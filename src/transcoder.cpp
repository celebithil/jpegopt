// SPDX-License-Identifier: Apache-2.0

#include "transcoder.h"

#include <csetjmp>
#include <cstring>

#include "jpeglib.h"

extern "C" {
#include "transupp.h"
}

namespace {

struct JpegErrorState {
    jpeg_error_mgr pub;
    jmp_buf jmp;
    std::string message;
    unsigned char* outbuf = nullptr;
    unsigned long outsize = 0;
};

// thread_local so that values written between setjmp and longjmp remain
// well-defined after longjmp (avoiding the usual UB with automatic vars).
thread_local JpegErrorState src_err;
thread_local JpegErrorState dst_err;

void jpeg_error_handler(j_common_ptr cinfo) {
    JpegErrorState* state = reinterpret_cast<JpegErrorState*>(cinfo->err);
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    state->message = buffer;
    longjmp(state->jmp, 1);
}

// Builds a custom progressive scan script. libjpeg's validation requires:
// AC scans must cover a single component; the Ah/Al sequence of each
// coefficient must step down by 1 (first Ah=0, then Ah=prev Al, Al=Ah-1).
bool build_scan_script(const jpeg_compress_struct& cinfo, ScanStyle style,
                       std::vector<jpeg_scan_info>& script) {
    const int ncomps = cinfo.num_components;
    if (ncomps <= 0 || ncomps > MAX_COMPS_IN_SCAN) return false;

    auto add_dc_scan = [&](int ah, int al) {
        jpeg_scan_info s{};
        s.comps_in_scan = ncomps;
        for (int ci = 0; ci < ncomps; ++ci) s.component_index[ci] = ci;
        s.Ss = 0;
        s.Se = 0;
        s.Ah = ah;
        s.Al = al;
        script.push_back(s);
    };
    auto add_ac_scan = [&](int comp, int ss, int se, int ah, int al) {
        jpeg_scan_info s{};
        s.comps_in_scan = 1;
        s.component_index[0] = comp;
        s.Ss = ss;
        s.Se = se;
        s.Ah = ah;
        s.Al = al;
        script.push_back(s);
    };
    auto add_interleaved_scan = [&](const std::vector<int>& comps, int ss, int se,
                                    int ah, int al) {
        jpeg_scan_info s{};
        s.comps_in_scan = static_cast<int>(comps.size());
        for (size_t i = 0; i < comps.size(); ++i) s.component_index[i] = comps[i];
        s.Ss = ss;
        s.Se = se;
        s.Ah = ah;
        s.Al = al;
        script.push_back(s);
    };
    std::vector<int> all_comps;
    for (int c = 0; c < ncomps; ++c) all_comps.push_back(c);

    // Generic band-family: DC (plain or successive-approximation) followed by
    // AC band passes. Each coefficient is sent in successive-approximation
    // steps: first pass (Ah=0, Al=first_al), then refinements stepping Al down
    // by 1 to 0, which is exactly libjpeg's required Ah/Al sequence. With
    // dc_plain the DC is a single (0,0) scan and only the AC passes refine.
    auto add_band_script = [&](const std::vector<std::pair<int, int>>& ac_bands,
                               int first_al, bool dc_plain) {
        if (dc_plain) add_dc_scan(0, 0);
        for (int al = first_al; al >= 0; --al) {
            const int ah = (al == first_al) ? 0 : al + 1;
            if (!dc_plain) add_dc_scan(ah, al);
            for (int c = 0; c < ncomps; ++c) {
                for (const auto& b : ac_bands) add_ac_scan(c, b.first, b.second, ah, al);
            }
        }
    };

    // mozjpeg's max-compression structure (jcparam.c, JCP_MAX_COMPRESSION),
    // parameterized for decomposition: dc_sa in {0,1} enables DC successive
    // approximation, luma_split is the luma AC spectral split point,
    // luma_al the luma first-pass Al, chroma_sa in {0,1} enables chroma AC
    // successive approximation. For 3-component YCbCr we reproduce the exact
    // asymmetric scan list; otherwise a generic all-component form.
    auto add_mozmax_script = [&](bool dc_sa, int luma_split, int luma_al,
                                 bool chroma_sa) {
        auto mozmax_generic = [&]() {
            if (dc_sa) add_dc_scan(0, 1);
            else add_dc_scan(0, 0);
            for (int c = 0; c < ncomps; ++c) add_ac_scan(c, 1, luma_split, 0, luma_al);
            for (int c = 0; c < ncomps; ++c) add_ac_scan(c, luma_split + 1, 63, 0, luma_al);
            for (int al = luma_al - 1; al >= 0; --al)
                for (int c = 0; c < ncomps; ++c) add_ac_scan(c, 1, 63, al + 1, al);
            if (dc_sa) add_dc_scan(1, 0);
        };
        if (ncomps != 3) {
            mozmax_generic();
            return;
        }
        if (dc_sa) add_dc_scan(0, 1);
        else add_dc_scan(0, 0);
        add_ac_scan(0, 1, luma_split, 0, luma_al);
        if (chroma_sa) {
            add_ac_scan(2, 1, 8, 0, 1);
            add_ac_scan(1, 1, 8, 0, 1);
            add_ac_scan(2, 9, 63, 0, 1);
            add_ac_scan(1, 9, 63, 0, 1);
        } else {
            add_ac_scan(2, 1, 8, 0, 0);
            add_ac_scan(1, 1, 8, 0, 0);
        }
        add_ac_scan(0, luma_split + 1, 63, 0, luma_al);
        for (int al = luma_al - 1; al >= 0; --al)
            add_ac_scan(0, 1, 63, al + 1, al);
        if (dc_sa) add_dc_scan(1, 0);
        if (chroma_sa) {
            add_ac_scan(2, 1, 8, 1, 0);
            add_ac_scan(1, 1, 8, 1, 0);
            add_ac_scan(2, 9, 63, 1, 0);
            add_ac_scan(1, 9, 63, 1, 0);
        } else {
            add_ac_scan(2, 9, 63, 0, 0);
            add_ac_scan(1, 9, 63, 0, 0);
        }
    };

    // mozjpeg's JCP_DEFAULT script (jcparam.c): separate luma/chroma DC scans,
    // per-component AC bands, no successive approximation. AC scans are
    // single-component (libjpeg requires this for progressive JPEG).
    auto add_mozdefault_script = [&]() {
        if (ncomps == 3) {
            add_interleaved_scan({0}, 0, 0, 0, 0);
            add_interleaved_scan({1, 2}, 0, 0, 0, 0);
            add_ac_scan(0, 1, 8, 0, 0);
            add_ac_scan(1, 1, 8, 0, 0);
            add_ac_scan(2, 1, 8, 0, 0);
            add_ac_scan(0, 9, 63, 0, 0);
            add_ac_scan(1, 9, 63, 0, 0);
            add_ac_scan(2, 9, 63, 0, 0);
        } else {
            add_interleaved_scan(all_comps, 0, 0, 0, 0);
            for (int c = 0; c < ncomps; ++c) {
                add_ac_scan(c, 1, 8, 0, 0);
                add_ac_scan(c, 9, 63, 0, 0);
            }
        }
    };

    // mozjpeg's JCP_FAST script (jcparam.c): interleaved DC plus one AC scan
    // per component, no SA.
    auto add_mozfast_script = [&]() {
        add_interleaved_scan(all_comps, 0, 0, 0, 0);
        for (int c = 0; c < ncomps; ++c) add_ac_scan(c, 1, 63, 0, 0);
    };

    // mozjpeg-family styles and their parameters.
    switch (style) {
        case ScanStyle::kMozMax:       add_mozmax_script(false, 8, 2, false); return true;
        case ScanStyle::kMozMaxDcSa1:  add_mozmax_script(true, 8, 2, false); return true;
        case ScanStyle::kMozMaxSplit5: add_mozmax_script(false, 5, 2, false); return true;
        case ScanStyle::kMozMaxChromaSa: add_mozmax_script(false, 8, 2, true); return true;
        case ScanStyle::kMozMaxAl3:    add_mozmax_script(false, 8, 3, false); return true;
        case ScanStyle::kMozMaxAl4:    add_mozmax_script(false, 8, 4, false); return true;
        case ScanStyle::kMozMaxSplit5Al3: add_mozmax_script(false, 5, 3, false); return true;
        case ScanStyle::kMozMaxAl3ChromaSa: add_mozmax_script(false, 8, 3, true); return true;
        case ScanStyle::kMozDefault:   add_mozdefault_script(); return true;
        case ScanStyle::kMozFast:      add_mozfast_script(); return true;
        default:
            break;
    }

    struct BandStyle {
        ScanStyle style;
        std::vector<std::pair<int, int>> ac_bands;
        int first_al;
        bool dc_plain;
    };
    static const BandStyle kBandStyles[] = {
        {ScanStyle::kFewScan, {{1, 63}}, 1, false},
        {ScanStyle::kFewScanAl2, {{1, 63}}, 2, false},
        {ScanStyle::kFewScanAl3, {{1, 63}}, 3, false},
        {ScanStyle::kFewScanAl4, {{1, 63}}, 4, false},
        {ScanStyle::kFineBands, {{1, 4}, {5, 8}, {9, 16}, {17, 32}, {33, 63}}, 1, false},
        {ScanStyle::kMediumBands, {{1, 8}, {9, 16}, {17, 31}, {32, 63}}, 1, false},
        {ScanStyle::kSuperFine,
         {{1, 3}, {4, 6}, {7, 10}, {11, 16}, {17, 24}, {25, 32}, {33, 63}}, 1, false},
        {ScanStyle::kFewScanAl1DcPlain, {{1, 63}}, 1, true},
        {ScanStyle::kFewScanAl2DcPlain, {{1, 63}}, 2, true},
        {ScanStyle::kFewScanAl3DcPlain, {{1, 63}}, 3, true},
        {ScanStyle::kFewScanAl4DcPlain, {{1, 63}}, 4, true},
        {ScanStyle::kFewScanAl5DcPlain, {{1, 63}}, 5, true},
        {ScanStyle::kFewScanAl6DcPlain, {{1, 63}}, 6, true},
        {ScanStyle::kFineBandsDcPlain,
         {{1, 4}, {5, 8}, {9, 16}, {17, 32}, {33, 63}}, 1, true},
        {ScanStyle::kMediumBandsDcPlain,
         {{1, 8}, {9, 16}, {17, 31}, {32, 63}}, 1, true},
        {ScanStyle::kMediumBandsAl2,
         {{1, 8}, {9, 16}, {17, 31}, {32, 63}}, 2, false},
        {ScanStyle::kSuperFineAl2,
         {{1, 3}, {4, 6}, {7, 10}, {11, 16}, {17, 24}, {25, 32}, {33, 63}}, 2, false},
        {ScanStyle::kSuperFineAl2DcPlain,
         {{1, 3}, {4, 6}, {7, 10}, {11, 16}, {17, 24}, {25, 32}, {33, 63}}, 2, true},
        {ScanStyle::kSuperFineAl3,
         {{1, 3}, {4, 6}, {7, 10}, {11, 16}, {17, 24}, {25, 32}, {33, 63}}, 3, false},
        {ScanStyle::kSuperFineAl3DcPlain,
         {{1, 3}, {4, 6}, {7, 10}, {11, 16}, {17, 24}, {25, 32}, {33, 63}}, 3, true},
    };
    for (const auto& bs : kBandStyles) {
        if (bs.style == style) {
            add_band_script(bs.ac_bands, bs.first_al, bs.dc_plain);
            return true;
        }
    }
    return false;
}

}  // namespace

bool lossless_transcode(const std::vector<uint8_t>& src, const TranscodeOptions& opt,
                        std::vector<uint8_t>& out, std::string& err) {
    struct jpeg_decompress_struct srcinfo;
    struct jpeg_compress_struct dstinfo;
    memset(&srcinfo, 0, sizeof(srcinfo));
    memset(&dstinfo, 0, sizeof(dstinfo));
    memset(&src_err, 0, sizeof(src_err));
    memset(&dst_err, 0, sizeof(dst_err));

    srcinfo.err = jpeg_std_error(&src_err.pub);
    src_err.pub.error_exit = jpeg_error_handler;
    dstinfo.err = jpeg_std_error(&dst_err.pub);
    dst_err.pub.error_exit = jpeg_error_handler;

    out.clear();
    if (src.empty()) {
        err = "empty input";
        return false;
    }

    if (setjmp(src_err.jmp)) {
        err = "decode: " + src_err.message;
        if (src_err.outbuf) free(src_err.outbuf);
        jpeg_destroy_decompress(&srcinfo);
        return false;
    }
    if (setjmp(dst_err.jmp)) {
        err = "encode: " + dst_err.message;
        if (dst_err.outbuf) free(dst_err.outbuf);
        jpeg_destroy_compress(&dstinfo);
        jpeg_destroy_decompress(&srcinfo);
        return false;
    }

    jpeg_create_decompress(&srcinfo);
    jpeg_mem_src(&srcinfo, src.data(), static_cast<unsigned long>(src.size()));
    jcopy_markers_setup(&srcinfo, opt.strip_metadata ? JCOPYOPT_NONE : JCOPYOPT_ALL);
    if (jpeg_read_header(&srcinfo, TRUE) != JPEG_HEADER_OK) {
        err = "decode: not a valid JPEG";
        jpeg_destroy_decompress(&srcinfo);
        return false;
    }

    jvirt_barray_ptr* src_coef_arrays = jpeg_read_coefficients(&srcinfo);

    jpeg_create_compress(&dstinfo);
    jpeg_mem_dest(&dstinfo, &dst_err.outbuf, &dst_err.outsize);
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

    if (opt.strip_restart) {
        dstinfo.restart_interval = 0;
        dstinfo.restart_in_rows = 0;
    }

    std::vector<jpeg_scan_info> custom_script;
    if (opt.progressive) {
        dstinfo.progressive_mode = TRUE;
        if (opt.scan_override && !opt.scan_override->empty()) {
            dstinfo.num_scans = static_cast<int>(opt.scan_override->size());
            dstinfo.scan_info = const_cast<jpeg_scan_info*>(opt.scan_override->data());
        } else if (opt.scan_style == ScanStyle::kLibJpeg) {
            dstinfo.num_scans = 0;
            dstinfo.scan_info = nullptr;
            jpeg_simple_progression(&dstinfo);
        } else if (build_scan_script(dstinfo, opt.scan_style, custom_script)) {
            dstinfo.num_scans = static_cast<int>(custom_script.size());
            dstinfo.scan_info = custom_script.data();
        } else {
            dstinfo.num_scans = 0;
            dstinfo.scan_info = nullptr;
            jpeg_simple_progression(&dstinfo);
        }
    }
    if (opt.arith) {
        dstinfo.arith_code = TRUE;
    }
    if (opt.optimize) {
        dstinfo.optimize_coding = TRUE;
    }

    jpeg_write_coefficients(&dstinfo, src_coef_arrays);
    jcopy_markers_execute(&srcinfo, &dstinfo, opt.strip_metadata ? JCOPYOPT_NONE : JCOPYOPT_ALL);
    jpeg_finish_compress(&dstinfo);

    if (dst_err.outsize > 0 && dst_err.outbuf) {
        out.assign(dst_err.outbuf, dst_err.outbuf + dst_err.outsize);
        free(dst_err.outbuf);
    } else {
        err = "encode produced no data";
        jpeg_destroy_compress(&dstinfo);
        jpeg_destroy_decompress(&srcinfo);
        return false;
    }

    jpeg_finish_decompress(&srcinfo);
    jpeg_destroy_compress(&dstinfo);
    jpeg_destroy_decompress(&srcinfo);
    return true;
}
