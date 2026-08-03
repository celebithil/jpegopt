// SPDX-License-Identifier: Apache-2.0

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "cli.h"
#include "guetzli_encode.h"
#include "jpeg_reader.h"
#include "pipeline.h"
#include "transcoder.h"
#include "util.h"
#include "verify.h"
#include "jpeglib.h"
#include "turbojpeg.h"

namespace {

int failures = 0;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);     \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

void make_synthetic_rgb(int width, int height, std::vector<uint8_t>& rgb) {
    rgb.resize(static_cast<size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t o = (static_cast<size_t>(y) * width + x) * 3;
            rgb[o + 0] = static_cast<uint8_t>((x * 255 / width) + ((y * 37 + x * 13) & 0x3f) % 40);
            rgb[o + 1] = static_cast<uint8_t>((y * 255 / height) + ((x * 29 + y * 7) & 0x1f) % 30);
            rgb[o + 2] = static_cast<uint8_t>(((x + y) * 255 / (width + height)) +
                                              ((x * 11 + y * 17) & 0x1f) % 25);
        }
    }
}

bool compress_rgb(const std::vector<uint8_t>& rgb, int width, int height, int quality,
                  bool progressive, std::vector<uint8_t>& jpeg, std::string& err) {
    tjhandle handle = tjInitCompress();
    if (!handle) return false;
    unsigned char* buf = nullptr;
    unsigned long size = 0;
    int flags = progressive ? TJFLAG_PROGRESSIVE : 0;
    int rc = tjCompress2(handle, rgb.data(), width, 0, height, TJPF_RGB, &buf, &size,
                         TJSAMP_420, quality, flags);
    if (rc != 0) {
        err = tjGetErrorStr();
        tjDestroy(handle);
        return false;
    }
    jpeg.assign(buf, buf + size);
    tjFree(buf);
    tjDestroy(handle);
    return true;
}

// Encodes an RGB fixture with libjpeg directly. When rows_per_restart > 0 the
// output carries DRI/RSTn restart markers; when add_markers is set it also
// carries a fake APP1 (EXIF) and COM marker for metadata-stripping tests.
bool compress_rgb_fixture(const std::vector<uint8_t>& rgb, int width, int height,
                          int quality, int rows_per_restart, bool add_markers,
                          std::vector<uint8_t>& jpeg, std::string& err) {
    struct jpeg_compress_struct cinfo {};
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    unsigned char* buf = nullptr;
    unsigned long size = 0;
    jpeg_mem_dest(&cinfo, &buf, &size);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    if (rows_per_restart > 0) cinfo.restart_in_rows = rows_per_restart;
    jpeg_start_compress(&cinfo, TRUE);
    if (add_markers) {
        static const JOCTET kFakeExif[] = {'F', 'a', 'k', 'e', 'E', 'x', 'i', 'f',
                                           '\0', '1', '2', '3', '4', '5'};
        jpeg_write_marker(&cinfo, JPEG_APP0 + 1, kFakeExif, sizeof(kFakeExif));
        static const JOCTET kComment[] = {'j', 'p', 'e', 'g', 'o', 'p',
                                          't', ' ', 't', 'e', 's', 't'};
        jpeg_write_marker(&cinfo, JPEG_COM, kComment, sizeof(kComment));
    }
    JSAMPROW row_pointer[1];
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = const_cast<JSAMPROW>(
            rgb.data() + cinfo.next_scanline * static_cast<size_t>(width) * 3);
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    jpeg.assign(buf, buf + size);
    free(buf);
    return true;
}

bool decode_and_compare(const std::vector<uint8_t>& jpeg, const DecodedImage& ref) {
    std::string err;
    DecodedImage img;
    if (!decode_rgb(jpeg, img, err)) {
        fprintf(stderr, "  decode failed: %s\n", err.c_str());
        return false;
    }
    return same_image(ref, img);
}

void test_transcode_roundtrip() {
    std::vector<uint8_t> rgb;
    make_synthetic_rgb(320, 240, rgb);
    std::vector<uint8_t> base, prog;
    std::string err;
    CHECK(compress_rgb(rgb, 320, 240, 85, false, base, err), "compress baseline");
    CHECK(compress_rgb(rgb, 320, 240, 85, true, prog, err), "compress progressive");

    DecodedImage ref;
    CHECK(decode_rgb(base, ref, err), "decode baseline reference");

    // baseline -> progressive+optimize
    std::vector<uint8_t> t1;
    CHECK(lossless_transcode(base, {true, false, true}, t1, err), "transcode progressive");
    CHECK(decode_and_compare(t1, ref), "progressive transcode lossless");

    // baseline -> optimized sequential ("baseline" candidate)
    std::vector<uint8_t> tb;
    TranscodeOptions ob;
    ob.optimize = true;
    CHECK(lossless_transcode(base, ob, tb, err), "transcode optimized baseline");
    CHECK(decode_and_compare(tb, ref), "optimized baseline lossless");

    // custom progressive scan scripts must be valid and lossless
    TranscodeOptions op1;
    op1.progressive = true;
    op1.optimize = true;
    op1.scan_style = ScanStyle::kFewScan;
    std::vector<uint8_t> t1a;
    CHECK(lossless_transcode(base, op1, t1a, err), "transcode progressive-min");
    JpegInfo ip1 = read_jpeg_info(t1a);
    CHECK(ip1.valid && ip1.is_progressive, "progressive-min is progressive");
    CHECK(decode_and_compare(t1a, ref), "progressive-min lossless");

    TranscodeOptions op2;
    op2.progressive = true;
    op2.optimize = true;
    op2.scan_style = ScanStyle::kFineBands;
    std::vector<uint8_t> t1b;
    CHECK(lossless_transcode(base, op2, t1b, err), "transcode progressive-bands");
    JpegInfo ip2 = read_jpeg_info(t1b);
    CHECK(ip2.valid && ip2.is_progressive, "progressive-bands is progressive");
    CHECK(decode_and_compare(t1b, ref), "progressive-bands lossless");

    TranscodeOptions op3;
    op3.progressive = true;
    op3.optimize = true;
    op3.scan_style = ScanStyle::kMediumBands;
    std::vector<uint8_t> t1c;
    CHECK(lossless_transcode(base, op3, t1c, err), "transcode progressive-medium");
    JpegInfo ip3 = read_jpeg_info(t1c);
    CHECK(ip3.valid && ip3.is_progressive, "progressive-medium is progressive");
    CHECK(decode_and_compare(t1c, ref), "progressive-medium lossless");

    TranscodeOptions op4;
    op4.progressive = true;
    op4.optimize = true;
    op4.scan_style = ScanStyle::kSuperFine;
    std::vector<uint8_t> t1d;
    CHECK(lossless_transcode(base, op4, t1d, err), "transcode progressive-fine");
    JpegInfo ip4 = read_jpeg_info(t1d);
    CHECK(ip4.valid && ip4.is_progressive, "progressive-fine is progressive");
    CHECK(decode_and_compare(t1d, ref), "progressive-fine lossless");

    TranscodeOptions op5;
    op5.progressive = true;
    op5.optimize = true;
    op5.scan_style = ScanStyle::kFewScanAl2;
    std::vector<uint8_t> t1e;
    CHECK(lossless_transcode(base, op5, t1e, err), "transcode progressive-al2");
    JpegInfo ip5 = read_jpeg_info(t1e);
    CHECK(ip5.valid && ip5.is_progressive, "progressive-al2 is progressive");
    CHECK(decode_and_compare(t1e, ref), "progressive-al2 lossless");

    TranscodeOptions op6;
    op6.progressive = true;
    op6.optimize = true;
    op6.scan_style = ScanStyle::kFewScanAl3;
    std::vector<uint8_t> t1f;
    CHECK(lossless_transcode(base, op6, t1f, err), "transcode progressive-al3");
    JpegInfo ip6 = read_jpeg_info(t1f);
    CHECK(ip6.valid && ip6.is_progressive, "progressive-al3 is progressive");
    CHECK(decode_and_compare(t1f, ref), "progressive-al3 lossless");

    TranscodeOptions op7;
    op7.progressive = true;
    op7.optimize = true;
    op7.scan_style = ScanStyle::kFewScanAl4;
    std::vector<uint8_t> t1g;
    CHECK(lossless_transcode(base, op7, t1g, err), "transcode progressive-al4");
    JpegInfo ip7 = read_jpeg_info(t1g);
    CHECK(ip7.valid && ip7.is_progressive, "progressive-al4 is progressive");
    CHECK(decode_and_compare(t1g, ref), "progressive-al4 lossless");

    TranscodeOptions op8;
    op8.progressive = true;
    op8.optimize = true;
    op8.scan_style = ScanStyle::kMozMax;
    std::vector<uint8_t> t1h;
    CHECK(lossless_transcode(base, op8, t1h, err), "transcode progressive-mozmax");
    JpegInfo ip8 = read_jpeg_info(t1h);
    CHECK(ip8.valid && ip8.is_progressive, "progressive-mozmax is progressive");
    CHECK(decode_and_compare(t1h, ref), "progressive-mozmax lossless");

    // sweep styles: every variant must yield a valid progressive JPEG that is
    // pixel-identical to the source.
    const ScanStyle sweep_styles[] = {
        ScanStyle::kFewScanAl1DcPlain, ScanStyle::kFewScanAl2DcPlain,
        ScanStyle::kFewScanAl3DcPlain, ScanStyle::kFewScanAl4DcPlain,
        ScanStyle::kMozMaxDcSa1,        ScanStyle::kMozMaxSplit5,
        ScanStyle::kMozMaxChromaSa,     ScanStyle::kMozMaxAl3,
        ScanStyle::kFewScanAl5DcPlain,  ScanStyle::kFewScanAl6DcPlain,
        ScanStyle::kMozMaxAl4,          ScanStyle::kMozMaxSplit5Al3,
        ScanStyle::kMozMaxAl3ChromaSa,
        ScanStyle::kFineBandsDcPlain,   ScanStyle::kMediumBandsDcPlain,
        ScanStyle::kMediumBandsAl2,     ScanStyle::kMozDefault,
        ScanStyle::kMozFast,            ScanStyle::kSuperFineAl2,
        ScanStyle::kSuperFineAl2DcPlain, ScanStyle::kSuperFineAl3,
        ScanStyle::kSuperFineAl3DcPlain,
    };
    for (ScanStyle s : sweep_styles) {
        TranscodeOptions op;
        op.progressive = true;
        op.optimize = true;
        op.scan_style = s;
        std::vector<uint8_t> t;
        CHECK(lossless_transcode(base, op, t, err), "transcode sweep style");
        JpegInfo ip = read_jpeg_info(t);
        CHECK(ip.valid && ip.is_progressive, "sweep style is progressive");
        CHECK(decode_and_compare(t, ref), "sweep style lossless");
    }

    // Source-script preservation: extract the progressive fixture's own scan
    // script, re-encode with it (scan_override), and check the output is both
    // lossless and uses the identical script.
    DecodedImage refpg;
    CHECK(decode_rgb(prog, refpg, err), "decode progressive ref");
    std::vector<JpegScan> src_scans;
    CHECK(extract_scan_script(prog, src_scans) && !src_scans.empty(),
          "extract source scan script");
    std::vector<jpeg_scan_info> override_script;
    for (const auto& s : src_scans) {
        jpeg_scan_info j{};
        j.comps_in_scan = static_cast<int>(s.comps.size());
        for (size_t i = 0; i < s.comps.size(); ++i) j.component_index[i] = s.comps[i];
        j.Ss = s.ss;
        j.Se = s.se;
        j.Ah = s.ah;
        j.Al = s.al;
        override_script.push_back(j);
    }
    TranscodeOptions opv;
    opv.progressive = true;
    opv.optimize = true;
    opv.scan_override = &override_script;
    std::vector<uint8_t> tv;
    CHECK(lossless_transcode(prog, opv, tv, err), "transcode source script");
    JpegInfo ipv = read_jpeg_info(tv);
    CHECK(ipv.valid && ipv.is_progressive, "source-script output is progressive");
    std::vector<JpegScan> out_scans;
    CHECK(extract_scan_script(tv, out_scans) && out_scans.size() == src_scans.size(),
          "source-script output keeps scan count");
    bool scripts_match = true;
    for (size_t i = 0; i < src_scans.size(); ++i) {
        if (out_scans[i].comps != src_scans[i].comps || out_scans[i].ss != src_scans[i].ss ||
            out_scans[i].se != src_scans[i].se || out_scans[i].ah != src_scans[i].ah ||
            out_scans[i].al != src_scans[i].al) {
            scripts_match = false;
        }
    }
    CHECK(scripts_match, "source-script output matches input script");
    CHECK(decode_and_compare(tv, refpg), "source-script re-encode lossless");

    // guetzli sequential encoder must reproduce the image exactly
    std::vector<uint8_t> gz;
    CHECK(guetzli_encode(base, false, gz, err), "guetzli encode baseline");
    JpegInfo ig = read_jpeg_info(gz);
    CHECK(ig.valid && !ig.is_progressive && !ig.is_arithmetic, "guetzli output sequential Huffman");
    CHECK(decode_and_compare(gz, ref), "guetzli lossless");
    std::vector<uint8_t> gzp;
    CHECK(guetzli_encode(prog, false, gzp, err), "guetzli encode progressive");
    CHECK(decode_and_compare(gzp, refpg), "guetzli on progressive lossless");

    // restart markers: strip them losslessly and shrink
    std::vector<uint8_t> rst;
    CHECK(compress_rgb_fixture(rgb, 320, 240, 85, 2, false, rst, err),
          "compress with restart");
    JpegInfo ir = read_jpeg_info(rst);
    CHECK(ir.valid && ir.restart_interval > 0, "restart fixture has DRI");
    std::vector<uint8_t> t5;
    TranscodeOptions o5;
    o5.optimize = true;
    o5.strip_restart = true;
    CHECK(lossless_transcode(rst, o5, t5, err), "transcode strip restart");
    JpegInfo i5 = read_jpeg_info(t5);
    CHECK(i5.valid && i5.restart_interval == 0, "restart markers stripped");
    DecodedImage refr;
    CHECK(decode_rgb(rst, refr, err), "decode rst ref");
    CHECK(decode_and_compare(t5, refr), "restart-stripped lossless");
    CHECK(t5.size() < rst.size(), "restart stripping shrinks");

    // metadata: fixture with APP1+COM, strip flag removes them, pixels stay
    std::vector<uint8_t> meta;
    CHECK(compress_rgb_fixture(rgb, 320, 240, 85, 0, true, meta, err),
          "compress with metadata");
    CHECK(count_markers(meta, 0xE1) >= 1 && count_markers(meta, 0xFE) >= 1,
          "fixture has APP1 and COM");

    TranscodeOptions o6a;
    o6a.optimize = true;
    std::vector<uint8_t> t6a;
    CHECK(lossless_transcode(meta, o6a, t6a, err), "transcode preserve metadata");
    CHECK(count_markers(t6a, 0xE1) >= 1 && count_markers(t6a, 0xFE) >= 1,
          "metadata preserved by default");

    TranscodeOptions o6b;
    o6b.optimize = true;
    o6b.strip_metadata = true;
    std::vector<uint8_t> t6b;
    CHECK(lossless_transcode(meta, o6b, t6b, err), "transcode strip metadata");
    CHECK(count_markers(t6b, 0xE1) == 0 && count_markers(t6b, 0xFE) == 0,
          "transcode strips APP1/COM");
    DecodedImage refm;
    CHECK(decode_rgb(meta, refm, err), "decode meta ref");
    CHECK(decode_and_compare(t6b, refm), "metadata-stripped lossless");

    std::vector<uint8_t> gz_keep, gz_strip;
    CHECK(guetzli_encode(meta, false, gz_keep, err), "guetzli preserve metadata");
    CHECK(count_markers(gz_keep, 0xE1) >= 1, "guetzli preserves APP1");
    CHECK(guetzli_encode(meta, true, gz_strip, err), "guetzli strip metadata");
    CHECK(count_markers(gz_strip, 0xE1) == 0 && count_markers(gz_strip, 0xFE) == 0,
          "guetzli strips APP1/COM");
    CHECK(decode_and_compare(gz_strip, refm), "guetzli stripped lossless");

    // baseline -> arithmetic
    std::vector<uint8_t> t2;
    CHECK(lossless_transcode(base, {false, true, true}, t2, err), "transcode arith");
    JpegInfo i2 = read_jpeg_info(t2);
    CHECK(i2.valid && i2.is_arithmetic, "arith output is arithmetic");
    DecodedImage d2;
    CHECK(decode_rgb(t2, d2, err), "decode arith");
    CHECK(same_image(ref, d2), "arith transcode lossless");

    // progressive -> progressive arith
    std::vector<uint8_t> t3;
    CHECK(lossless_transcode(prog, {true, true, true}, t3, err), "transcode progressive arith");
    DecodedImage d3;
    CHECK(decode_rgb(t3, d3, err), "decode progressive arith");
    DecodedImage refp;
    CHECK(decode_rgb(prog, refp, err), "decode progressive ref");
    CHECK(same_image(refp, d3), "progressive arith lossless");
}

void test_pipeline(std::vector<uint8_t>& jpeg, const std::string& label,
                   bool strip_metadata = false) {
    std::string dir = (std::filesystem::temp_directory_path() /
                       ("jpegopt-test-" + std::to_string(getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    std::string err;
    std::string src = dir + "/src.jpg";
    CHECK(util::write_file(src, jpeg, err), "write src");

    DecodedImage ref;
    CHECK(decode_rgb(jpeg, ref, err), "decode src ref");

    PipelineOptions opts;
    opts.allow_arith = true;
    opts.dry_run = false;
    opts.strip_metadata = strip_metadata;
    opts.temp_dir = dir + "/tmp";
    FileResult r = process_file(src, opts);

    if (!r.ok) {
        fprintf(stderr, "  (%s) pipeline error: %s\n", label.c_str(), r.error.c_str());
        ++failures;
        std::filesystem::remove_all(dir, ec);
        return;
    }

    CHECK(r.best_size <= r.original_size, "never larger than original");
    CHECK(r.strip_metadata == strip_metadata, "strip flag reported");

    // every candidate method must have been attempted and pixel-verified
    std::vector<std::string> required = {
        "progressive", "progressive-min",
        "progressive-bands", "progressive-fine",
        "progressive-al3", "progressive-mozmax",
        "progressive-dcplain", "progressive-al2-dcplain",
        "progressive-al3-dcplain", "progressive-al4-dcplain",
        "progressive-mozmax-dcsa", "progressive-mozmax-split5",
        "progressive-mozmax-chromasa", "progressive-mozmax-al3",
        "progressive-al5-dcplain", "progressive-al6-dcplain",
        "progressive-mozmax-al4", "progressive-mozmax-split5-al3",
        "progressive-mozmax-al3-chromasa",
        "progressive-bands-dcplain", "progressive-medium-dcplain",
        "progressive-moz-default", "progressive-moz-fast",
        "guetzli", "arith",
        "progressive+arith", "progressive+arith-min",
        "progressive+arith-al2", "progressive+arith-al3",
        "progressive+arith-al4", "progressive+arith-mozmax",
        "progressive+arith-dcplain", "progressive+arith-al2-dcplain",
        "progressive+arith-al3-dcplain", "progressive+arith-al4-dcplain",
        "progressive+arith-mozmax-al3",
        "progressive+arith-al5-dcplain", "progressive+arith-al6-dcplain",
        "progressive+arith-mozmax-al4", "progressive+arith-mozmax-split5-al3",
        "progressive+arith-mozmax-al3-chromasa",
        "progressive+arith-medium-al2", "progressive+arith-moz-default",
        "progressive+arith-superfine-al2", "progressive+arith-superfine-al3",
    };
    for (const std::string& m : required) {
        bool found = false;
        for (const auto& c : r.candidates) {
            if (c.method == m) {
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "  (%s) missing candidate: %s\n", label.c_str(), m.c_str());
            ++failures;
        }
    }

    if (r.changed) {
        std::string out = util::output_path_for(src);
        CHECK(util::file_exists(out), "output written");
        std::vector<uint8_t> out_bytes;
        CHECK(util::read_file(out, out_bytes, err), "read output");
        DecodedImage out_img;
        CHECK(decode_rgb(out_bytes, out_img, err), "decode output");
        CHECK(same_image(ref, out_img), "output pixel-identical to source");
        if (strip_metadata) {
            CHECK(count_markers(out_bytes, 0xE1) == 0 && count_markers(out_bytes, 0xFE) == 0,
                  "output has no copied APP1/COM markers");
        }
        printf("  %s: %s -> %s (%s)%s%s\n", label.c_str(),
               util::format_bytes(r.original_size).c_str(),
               util::format_bytes(r.best_size).c_str(),
               r.method.c_str(), r.arith_used ? " [arith]" : "",
               r.strip_metadata ? " [meta-stripped]" : "");
        std::filesystem::remove(out, ec);
    } else {
        printf("  %s: unchanged%s\n", label.c_str(),
               r.strip_metadata ? " (meta-stripped)" : "");
    }

    std::filesystem::remove_all(dir, ec);
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_transcode_roundtrip();

    std::vector<uint8_t> rgb;
    make_synthetic_rgb(640, 480, rgb);
    std::string err;

    std::vector<uint8_t> base;
    CHECK(compress_rgb(rgb, 640, 480, 85, false, base, err), "compress baseline");
    test_pipeline(base, "baseline");
    test_pipeline(base, "baseline+strip-meta", true);

    std::vector<uint8_t> prog;
    CHECK(compress_rgb(rgb, 640, 480, 85, true, prog, err), "compress progressive");
    test_pipeline(prog, "progressive");

    std::vector<uint8_t> lowq;
    CHECK(compress_rgb(rgb, 640, 480, 30, false, lowq, err), "compress lowq");
    test_pipeline(lowq, "low-quality");

    if (failures == 0) {
        printf("All tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test(s) failed.\n", failures);
    return 1;
}
