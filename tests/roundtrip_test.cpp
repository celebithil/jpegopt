// SPDX-License-Identifier: Apache-2.0

#include <unistd.h>
#include <sys/wait.h>

#include <cctype>
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
#include "report.h"
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
        static const JOCTET kFakeExif[] = {'E', 'x', 'i', 'f', '\0', '\0',
                                           '1', '2', '3', '4', '5', '6', '7'};
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
    if (!decode_native(jpeg, img, err)) {
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
    CHECK(decode_native(base, ref, err), "decode baseline reference");

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
    CHECK(decode_native(prog, refpg, err), "decode progressive ref");
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
    CHECK(decode_native(rst, refr, err), "decode rst ref");
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
    CHECK(decode_native(meta, refm, err), "decode meta ref");
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
    CHECK(decode_native(t2, d2, err), "decode arith");
    CHECK(same_image(ref, d2), "arith transcode lossless");

    // progressive -> progressive arith
    std::vector<uint8_t> t3;
    CHECK(lossless_transcode(prog, {true, true, true}, t3, err), "transcode progressive arith");
    DecodedImage d3;
    CHECK(decode_native(t3, d3, err), "decode progressive arith");
    DecodedImage refp;
    CHECK(decode_native(prog, refp, err), "decode progressive ref");
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
    CHECK(decode_native(jpeg, ref, err), "decode src ref");

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
        CHECK(decode_native(out_bytes, out_img, err), "decode output");
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

// Inserts a raw APP1 (EXIF) segment right after SOI. Used to force a large
// strippable marker so the granular-strip pipeline test is deterministic.
void insert_app1(std::vector<uint8_t>& jpeg, const std::string& payload) {
    if (jpeg.size() < 2 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) return;
    const size_t seg_len = payload.size() + 2;
    std::vector<uint8_t> out;
    out.reserve(jpeg.size() + 2 + seg_len);
    out.push_back(0xFF);
    out.push_back(0xD8);
    out.push_back(0xFF);
    out.push_back(0xE1);
    out.push_back(static_cast<uint8_t>((seg_len >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(seg_len & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
    out.insert(out.end(), jpeg.begin() + 2, jpeg.end());
    jpeg = std::move(out);
}

void test_marker_strip_unit() {
    std::vector<uint8_t> rgb;
    make_synthetic_rgb(320, 240, rgb);
    std::vector<uint8_t> meta;
    std::string err;
    CHECK(compress_rgb_fixture(rgb, 320, 240, 85, 0, true, meta, err),
          "compress metadata fixture");
    DecodedImage ref;
    CHECK(decode_native(meta, ref, err), "decode meta fixture");

    MarkerPolicy exif_only;
    exif_only.strip_exif = true;
    MarkerStripped cs = classify_strippable(meta, exif_only);
    CHECK(cs.exif && cs.any(), "classify finds EXIF");
    CHECK(cs.summary() == "exif", "classify summary is exif");

    std::vector<uint8_t> s1 = meta;
    strip_markers(s1, exif_only);
    CHECK(count_markers(s1, 0xE1) == 0, "strip removes APP1");
    CHECK(count_markers(s1, 0xFE) >= 1, "strip keeps COM");
    CHECK(decode_and_compare(s1, ref), "strip-exif lossless");

    MarkerPolicy com_only;
    com_only.strip_com = true;
    std::vector<uint8_t> s2 = meta;
    strip_markers(s2, com_only);
    CHECK(count_markers(s2, 0xFE) == 0, "strip removes COM");
    CHECK(count_markers(s2, 0xE1) >= 1, "strip keeps APP1");
    CHECK(decode_and_compare(s2, ref), "strip-com lossless");

    std::vector<uint8_t> s3 = meta;
    strip_markers(s3, MarkerPolicy{});
    CHECK(s3 == meta, "empty policy leaves bytes identical");

    MarkerPolicy both;
    both.strip_exif = true;
    both.strip_com = true;
    std::vector<uint8_t> s4 = meta;
    strip_markers(s4, both);
    CHECK(count_markers(s4, 0xE1) == 0 && count_markers(s4, 0xFE) == 0, "strip both");
    CHECK(decode_and_compare(s4, ref), "strip both lossless");
}

void test_pipeline_markers() {
    std::vector<uint8_t> rgb;
    make_synthetic_rgb(320, 240, rgb);
    std::vector<uint8_t> meta;
    std::string err;
    CHECK(compress_rgb_fixture(rgb, 320, 240, 85, 0, true, meta, err),
          "compress meta fixture");
    std::string big_exif("Exif\0\0", 6);
    big_exif.append(60000, 'A');
    insert_app1(meta, big_exif);

    std::string dir = (std::filesystem::temp_directory_path() /
                       ("jpegopt-mk-" + std::to_string(getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);
    std::string src = dir + "/src.jpg";
    CHECK(util::write_file(src, meta, err), "write marker src");

    PipelineOptions opts;
    opts.markers.strip_exif = true;
    opts.temp_dir = dir + "/tmp";
    FileResult r = process_file(src, opts);
    CHECK(r.ok, "pipeline with strip-exif ok");
    CHECK(r.changed, "large EXIF guarantees a smaller result");
    CHECK(r.stripped_markers.find("exif") != std::string::npos, "reports exif stripped");

    std::vector<uint8_t> out_bytes;
    CHECK(util::read_file(util::output_path_for(src), out_bytes, err), "read marker output");
    CHECK(count_markers(out_bytes, 0xE1) == 0, "output has no APP1");
    CHECK(count_markers(out_bytes, 0xFE) >= 1, "output keeps COM");
    DecodedImage ref;
    CHECK(decode_native(meta, ref, err), "decode marker src ref");
    CHECK(decode_and_compare(out_bytes, ref), "strip-exif output lossless");

    std::filesystem::remove_all(dir, ec);
}

void test_pipeline_threshold() {
    std::vector<uint8_t> rgb;
    make_synthetic_rgb(320, 240, rgb);
    std::vector<uint8_t> prog;
    std::string err;
    CHECK(compress_rgb(rgb, 320, 240, 85, true, prog, err), "compress progressive");

    std::string dir = (std::filesystem::temp_directory_path() /
                       ("jpegopt-th-" + std::to_string(getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);
    std::string src = dir + "/src.jpg";
    CHECK(util::write_file(src, prog, err), "write threshold src");

    PipelineOptions opts;
    opts.min_savings_pct = 90.0;
    opts.temp_dir = dir + "/tmp";
    FileResult r = process_file(src, opts);
    CHECK(r.ok, "pipeline with threshold ok");
    CHECK(!r.changed, "90% threshold blocks any write");
    CHECK(!util::file_exists(util::output_path_for(src)), "no output written below threshold");

    std::filesystem::remove_all(dir, ec);
}

// --------------------------------------------------------------------------
// CLI contract helpers: the exit-code and JSON tests drive the built binary,
// because exit codes and stdout text are properties of the process, not of the
// in-process library API.
// --------------------------------------------------------------------------

std::string g_tool_path = "build/jpegopt";

std::string shell_quote(const std::string& s) {
    std::string q = "'";
    for (char c : s) {
        if (c == '\'') q += "'\\''";
        else q += c;
    }
    q += "'";
    return q;
}

// Runs the tool with the given raw argument string, capturing stdout. Returns
// the process exit status, or -1 when the process could not be run at all.
int run_tool(const std::string& args, std::string& out) {
    std::string cmd = shell_quote(g_tool_path) + " " + args + " 2>/dev/null";
    out.clear();
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return -1;
    char buf[4096];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    int status = pclose(p);
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

// Same as run_tool, but feeds `input` on the child's stdin (for --files-stdin).
// popen() is unidirectional, so the child's stdout is routed to a temp file
// that is read back after it exits.
int run_tool_stdin(const std::string& args, const std::string& input, std::string& out) {
    out.clear();
    std::string capture = (std::filesystem::temp_directory_path() /
                           ("jpegopt-stdin-" + std::to_string(getpid()) + ".out")).string();
    std::filesystem::remove(capture);
    std::string cmd = shell_quote(g_tool_path) + " " + args + " > " +
                      shell_quote(capture) + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "w");
    if (!p) return -1;
    if (!input.empty()) fwrite(input.data(), 1, input.size(), p);
    int status = pclose(p);
    std::string err;
    std::vector<uint8_t> bytes;
    if (util::read_file(capture, bytes, err)) {
        out.assign(bytes.begin(), bytes.end());
    }
    std::filesystem::remove(capture);
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

// Splits the top-level objects of a JSON array, respecting strings/escapes so
// that braces inside candidate tags or paths cannot confuse the scan.
void find_top_level_objects(const std::string& s, std::vector<std::string>& out) {
    int depth = 0;
    bool in_str = false, esc = false;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{') { if (depth == 0) start = i; ++depth; }
        else if (c == '}') {
            --depth;
            if (depth == 0) out.push_back(s.substr(start, i - start + 1));
        }
    }
}

bool has_bool_key(const std::string& obj, const std::string& key) {
    return obj.find("\"" + key + "\":true") != std::string::npos ||
           obj.find("\"" + key + "\":false") != std::string::npos;
}

bool has_str_key(const std::string& obj, const std::string& key) {
    return obj.find("\"" + key + "\":\"") != std::string::npos;
}

bool has_int_key(const std::string& obj, const std::string& key) {
    size_t p = obj.find("\"" + key + "\":");
    if (p == std::string::npos) return false;
    p += key.size() + 3;
    return p < obj.size() && std::isdigit(static_cast<unsigned char>(obj[p]));
}

// Asserts the documented schema (docs/json-output.md) on one JSON object.
void check_json_object(const std::string& obj, const std::string& label) {
    struct { const char* key; } bools[] = {
        {"ok"}, {"changed"}, {"strip_metadata"}, {"arith"}, {"arith_fell_back"},
    };
    struct { const char* key; } strings[] = {
        {"path"}, {"stripped_markers"}, {"method"}, {"error"},
    };
    struct { const char* key; } ints[] = {
        {"original_size"}, {"best_size"},
    };
    for (const auto& k : bools) {
        CHECK(has_bool_key(obj, k.key), (label + ": bool key " + k.key).c_str());
    }
    for (const auto& k : strings) {
        CHECK(has_str_key(obj, k.key), (label + ": string key " + k.key).c_str());
    }
    for (const auto& k : ints) {
        CHECK(has_int_key(obj, k.key), (label + ": int key " + k.key).c_str());
    }
    CHECK(obj.find("\"candidates\":{") != std::string::npos,
          (label + ": candidates object").c_str());
}

void test_json_schema() {
    std::vector<uint8_t> rgb;
    make_synthetic_rgb(160, 120, rgb);
    std::vector<uint8_t> jpeg;
    std::string err;
    CHECK(compress_rgb(rgb, 160, 120, 85, false, jpeg, err), "json fixture compress");

    std::string dir = (std::filesystem::temp_directory_path() /
                       ("jpegopt-json-" + std::to_string(getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);
    std::string good = dir + "/good.jpg";
    std::string bad = dir + "/bad.jpg";
    CHECK(util::write_file(good, jpeg, err), "write json fixture");
    CHECK(util::write_file(bad, std::vector<uint8_t>{'n', 'o', 'p', 'e'}, err),
          "write bad json fixture");

    PipelineOptions opts;
    opts.dry_run = true;
    std::vector<FileResult> results;
    results.push_back(process_file(good, opts));
    results.push_back(process_file(bad, opts));
    CHECK(results[0].ok, "json fixture processed");
    CHECK(!results[1].ok, "bad fixture rejected");
    CHECK(results[0].best_size <= results[0].original_size, "json best <= original");

    std::string json;
    build_report(results, true, false, json);

    // A single array, one object per input, in input order.
    std::vector<std::string> objs;
    find_top_level_objects(json, objs);
    CHECK(objs.size() == results.size(), "json: one object per file");
    CHECK(!json.empty() && json.front() == '[', "json: array opens");

    for (size_t i = 0; i < objs.size(); ++i) {
        check_json_object(objs[i], "json");
    }

    // The failed file must report ok:false and a non-empty error.
    if (objs.size() >= 2) {
        CHECK(objs[1].find("\"ok\":false") != std::string::npos,
              "json: bad file ok=false");
        CHECK(objs[1].find("\"error\":\"\"") == std::string::npos,
              "json: bad file has an error message");
        CHECK(objs[1].find("\"candidates\":{}") != std::string::npos,
              "json: bad file has no candidates");
    }

    // The good file must carry at least one verified candidate.
    CHECK(objs[0].find("\"candidates\":{}") == std::string::npos,
          "json: good file has candidates");
    CHECK(objs[0].find("\"ok\":true") != std::string::npos, "json: good file ok=true");

    std::filesystem::remove_all(dir, ec);
}

void test_cli_exit_codes() {
    if (!std::filesystem::exists(g_tool_path)) {
        printf("  (skipping CLI exit-code tests: %s not built)\n", g_tool_path.c_str());
        return;
    }

    std::vector<uint8_t> rgb;
    make_synthetic_rgb(160, 120, rgb);
    std::vector<uint8_t> jpeg;
    std::string err;
    CHECK(compress_rgb(rgb, 160, 120, 85, false, jpeg, err), "exit fixture compress");

    std::string dir = (std::filesystem::temp_directory_path() /
                       ("jpegopt-exit-" + std::to_string(getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);
    std::string good = dir + "/good.jpg";
    std::string bad = dir + "/bad.jpg";
    CHECK(util::write_file(good, jpeg, err), "write exit fixture");
    CHECK(util::write_file(bad, std::vector<uint8_t>{'x', 'y'}, err), "write bad fixture");
    std::string empty_dir = dir + "/nojpeg";
    std::filesystem::create_directory(empty_dir, ec);

    std::string out;

    // 0: a normal, successful run.
    CHECK(run_tool("--dry-run " + shell_quote(good), out) == 0, "exit 0 on success");
    CHECK(out.find("good.jpg") != std::string::npos, "report names the file");

    // 0: --version and --help are informational success outcomes.
    CHECK(run_tool("--version", out) == 0, "exit 0 on --version");
    CHECK(out.find("jpegopt " JPEGOPT_VERSION) != std::string::npos,
          "--version prints the version");
    CHECK(run_tool("--help", out) == 0, "exit 0 on --help");
    CHECK(out.find("Usage:") != std::string::npos, "--help prints usage");

    // 2: usage errors — unknown option, no inputs, no JPEG in the input set.
    CHECK(run_tool("--definitely-not-an-option", out) == 2, "exit 2 on unknown option");
    CHECK(run_tool("", out) == 2, "exit 2 on no inputs");
    CHECK(run_tool("--dry-run " + shell_quote(empty_dir), out) == 2,
          "exit 2 when no JPEG is found");

    // 1: the input exists but cannot be processed.
    CHECK(run_tool("--dry-run " + shell_quote(bad), out) == 1, "exit 1 on bad JPEG");

    std::filesystem::remove_all(dir, ec);
}

// Grayscale fixture: 1 component, JCS_GRAYSCALE, baseline sequential.
bool compress_gray_fixture(int width, int height, int quality,
                           std::vector<uint8_t>& jpeg, std::string& err) {
    std::vector<uint8_t> gray(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            gray[static_cast<size_t>(y) * width + x] =
                static_cast<uint8_t>((x * 255 / width + y * 3) & 0xff);
        }
    }
    struct jpeg_compress_struct cinfo {};
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    unsigned char* buf = nullptr;
    unsigned long size = 0;
    jpeg_mem_dest(&cinfo, &buf, &size);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 1;
    cinfo.in_color_space = JCS_GRAYSCALE;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    JSAMPROW row_pointer[1];
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = const_cast<JSAMPROW>(
            gray.data() + cinfo.next_scanline * static_cast<size_t>(width));
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    jpeg.assign(buf, buf + size);
    free(buf);
    return true;
}

void test_edge_inputs() {
    std::string dir = (std::filesystem::temp_directory_path() /
                       ("jpegopt-edge-" + std::to_string(getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);
    std::string err;
    PipelineOptions opts;
    opts.dry_run = true;

    // Empty file: unreadable as an image, must be reported, not crash.
    std::string empty = dir + "/empty.jpg";
    CHECK(util::write_file(empty, std::vector<uint8_t>{}, err), "write empty");
    FileResult re = process_file(empty, opts);
    CHECK(!re.ok, "empty input is not ok");
    CHECK(!re.error.empty(), "empty input has an error");

    // Truncated JPEG: header of a real image, body cut off.
    std::vector<uint8_t> rgb;
    make_synthetic_rgb(160, 120, rgb);
    std::vector<uint8_t> jpeg;
    CHECK(compress_rgb(rgb, 160, 120, 85, false, jpeg, err), "edge fixture compress");
    std::vector<uint8_t> truncated(jpeg.begin(), jpeg.begin() + jpeg.size() / 2);
    std::string trunc = dir + "/trunc.jpg";
    CHECK(util::write_file(trunc, truncated, err), "write truncated");
    FileResult rt = process_file(trunc, opts);
    CHECK(!rt.ok, "truncated input is not ok");

    // Non-JPEG bytes with a .jpg name.
    std::string textf = dir + "/text.jpg";
    std::string junk = "this is definitely not a JPEG";
    CHECK(util::write_file(textf, std::vector<uint8_t>(junk.begin(), junk.end()), err),
          "write non-jpeg");
    FileResult rn = process_file(textf, opts);
    CHECK(!rn.ok, "non-JPEG input is not ok");

    // Very small but valid image.
    std::vector<uint8_t> tiny_rgb;
    make_synthetic_rgb(16, 16, tiny_rgb);
    std::vector<uint8_t> tiny;
    CHECK(compress_rgb(tiny_rgb, 16, 16, 85, false, tiny, err), "compress tiny");
    std::string tinyf = dir + "/tiny.jpg";
    CHECK(util::write_file(tinyf, tiny, err), "write tiny");
    FileResult rti = process_file(tinyf, opts);
    CHECK(rti.ok, "tiny input is ok");
    CHECK(rti.best_size <= rti.original_size, "tiny never grows");

    // Grayscale source: must be accepted and produce a lossless candidate.
    std::vector<uint8_t> gray;
    CHECK(compress_gray_fixture(160, 120, 85, gray, err), "compress grayscale");
    std::string grayf = dir + "/gray.jpg";
    CHECK(util::write_file(grayf, gray, err), "write grayscale");
    FileResult rg = process_file(grayf, opts);
    CHECK(rg.ok, "grayscale input is ok");
    if (rg.ok && rg.changed) {
        DecodedImage ref, out;
        CHECK(decode_native(gray, ref, err), "decode grayscale ref");
        CHECK(decode_native(rg.best_bytes, out, err), "decode grayscale candidate");
        CHECK(same_image(ref, out), "grayscale candidate lossless");
    }

    std::filesystem::remove_all(dir, ec);
}

// CMYK fixture: 4 components, JCS_CMYK, baseline sequential. Builds a
// grayscale-free device-CMYK JPEG that the tool must either optimize losslessly
// or reject cleanly (the vendored TurboJPEG decode path has no RGB conversion
// for CMYK), but never crash on.
bool compress_cmyk_fixture(int width, int height, int quality,
                           std::vector<uint8_t>& jpeg, std::string& err) {
    (void)err;
    std::vector<uint8_t> cmyk(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t o = (static_cast<size_t>(y) * width + x) * 4;
            cmyk[o + 0] = static_cast<uint8_t>((x * 4) & 0xff);
            cmyk[o + 1] = static_cast<uint8_t>((y * 4) & 0xff);
            cmyk[o + 2] = static_cast<uint8_t>(((x + y) * 2) & 0xff);
            cmyk[o + 3] = static_cast<uint8_t>((x * 3) & 0xff);
        }
    }
    struct jpeg_compress_struct cinfo {};
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    unsigned char* buf = nullptr;
    unsigned long size = 0;
    jpeg_mem_dest(&cinfo, &buf, &size);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 4;
    cinfo.in_color_space = JCS_CMYK;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    JSAMPROW row_pointer[1];
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = const_cast<JSAMPROW>(
            cmyk.data() + cinfo.next_scanline * static_cast<size_t>(width) * 4);
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    jpeg.assign(buf, buf + size);
    free(buf);
    return true;
}

void test_cmyk_input() {
    std::vector<uint8_t> cmyk;
    std::string err;
    CHECK(compress_cmyk_fixture(64, 64, 85, cmyk, err), "compress CMYK fixture");
    JpegInfo info = read_jpeg_info(cmyk);
    CHECK(info.valid && info.num_components == 4, "CMYK fixture has 4 components");

    std::string dir = (std::filesystem::temp_directory_path() /
                       ("jpegopt-cmyk-" + std::to_string(getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);
    std::string path = dir + "/cmyk.jpg";
    CHECK(util::write_file(path, cmyk, err), "write CMYK fixture");

    // CMYK is now a supported source class: it must optimize losslessly in its
    // own 4-channel space (the verification deliberately does not convert to
    // RGB, which is what used to make CMYK fail with "Unsupported color
    // conversion request").
    PipelineOptions opts;
    opts.dry_run = true;
    opts.allow_arith = true;
    FileResult r = process_file(path, opts);
    CHECK(r.ok, ("CMYK input is supported: " + r.error).c_str());
    if (r.ok) {
        CHECK(r.best_size <= r.original_size, "CMYK result never grows");
        DecodedImage ref;
        CHECK(decode_native(cmyk, ref, err), "decode CMYK reference");
        CHECK(ref.channels == 4, "CMYK reference has 4 channels");
        CHECK(ref.bit_depth == 8, "CMYK reference is 8-bit");
        if (!r.candidates.empty()) {
            CHECK(r.changed, "CMYK source yields an improvement");
            CHECK(!r.best_bytes.empty(), "CMYK winner carries bytes");
            DecodedImage out;
            CHECK(decode_native(r.best_bytes, out, err), "decode CMYK winner");
            CHECK(same_image(ref, out), "CMYK winner is pixel-identical in CMYK");
        }
    }

    // When the binary is available, a supported CMYK file must exit 0.
    if (std::filesystem::exists(g_tool_path)) {
        std::string out;
        int code = run_tool("--dry-run " + shell_quote(path), out);
        CHECK(code == (r.ok ? 0 : 1), "CMYK CLI exit matches pipeline result");
    }

    std::filesystem::remove_all(dir, ec);
}

// Locates the vendored 12-bit test image, which is the only 12-bit sample
// available in-tree.
std::string find_twelve_bit_fixture() {
    const char* candidates[] = {
        "vendor/libjpeg-turbo/libjpeg-turbo/testimages/testorig12.jpg",
        "../vendor/libjpeg-turbo/libjpeg-turbo/testimages/testorig12.jpg",
        "../../vendor/libjpeg-turbo/libjpeg-turbo/testimages/testorig12.jpg",
    };
    for (const char* c : candidates) {
        if (std::filesystem::exists(c)) return c;
    }
    return std::string();
}

void test_twelve_bit_input() {
    std::string fixture = find_twelve_bit_fixture();
    if (fixture.empty()) {
        printf("  (skipping 12-bit tests: vendored testorig12.jpg not found)\n");
        return;
    }

    std::vector<uint8_t> bytes;
    std::string err;
    CHECK(util::read_file(fixture, bytes, err), "read 12-bit fixture");

    // The source really is 12-bit, and decodes natively at that depth.
    DecodedImage ref;
    CHECK(decode_native(bytes, ref, err), ("decode 12-bit source: " + err).c_str());
    CHECK(ref.bit_depth == 12, "reference reports 12-bit precision");

    std::string dir = (std::filesystem::temp_directory_path() /
                       ("jpegopt-12bit-" + std::to_string(getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);
    std::string path = dir + "/twelve.jpg";
    CHECK(util::write_file(path, bytes, err), "write 12-bit fixture");

    PipelineOptions opts;
    opts.dry_run = true;
    opts.allow_arith = true;  // must be ignored for 12-bit, not attempted
    FileResult r = process_file(path, opts);
    CHECK(r.ok, ("12-bit input is supported: " + r.error).c_str());
    if (r.ok) {
        CHECK(r.changed, "12-bit source yields an improvement");
        CHECK(!r.best_bytes.empty(), "12-bit winner carries bytes");
        CHECK(r.best_size <= r.original_size, "12-bit result never grows");

        // The only candidate for a 12-bit source is the transform path, and it
        // must not be arithmetic-coded.
        bool only_transform = !r.candidates.empty();
        for (const auto& c : r.candidates) {
            if (c.method != "progressive-12bit" || c.arith) only_transform = false;
        }
        CHECK(only_transform, "12-bit source yields exactly the transform candidate");
        CHECK(!r.arith_used, "12-bit result is not arithmetic-coded");

        // Precision and pixels survive the round trip.
        DecodedImage out;
        CHECK(decode_native(r.best_bytes, out, err), "decode 12-bit winner");
        CHECK(out.bit_depth == 12, "12-bit precision preserved");
        CHECK(same_image(ref, out), "12-bit winner is pixel-identical");
    }

    std::filesystem::remove_all(dir, ec);
}

void test_file_lists() {
    // Direct parser check: the list grammar (one path per line, blank lines and
    // '#' comments skipped) is what --files-from/--files-stdin promise. This
    // runs in-process so a regression that only mangles the grammar cannot
    // hide behind paths that merely fail to exist.
    {
        std::string dir = (std::filesystem::temp_directory_path() /
                           ("jpegopt-parse-" + std::to_string(getpid()))).string();
        std::error_code ec;
        std::filesystem::create_directory(dir, ec);
        std::string lp = dir + "/p.txt";
        std::string content = "# comment\n\n/p1.jpg\n   \n/p2.jpg\n#x\n";
        std::string werr;
        util::write_file(lp, std::vector<uint8_t>(content.begin(), content.end()), werr);

        std::vector<std::string> argv_s = {"jpegopt", "--dry-run", "--files-from", lp};
        std::vector<char*> argv;
        for (auto& s : argv_s) argv.push_back(const_cast<char*>(s.c_str()));
        CliOptions opts;
        std::string perr;
        bool parsed = parse_cli(static_cast<int>(argv.size()), argv.data(), opts, perr);
        CHECK(parsed, "parse_cli accepts --files-from");
        CHECK(opts.inputs.size() == 2, "list grammar yields exactly two paths");
        if (opts.inputs.size() == 2) {
            CHECK(opts.inputs[0] == "/p1.jpg" && opts.inputs[1] == "/p2.jpg",
                  "list grammar skips comments and blanks, keeps order and trims");
        }

        // A missing list file is a parse error, not a silent empty set.
        std::vector<std::string> bad_s = {"jpegopt", "--files-from", dir + "/nope.txt"};
        std::vector<char*> bad_argv;
        for (auto& s : bad_s) bad_argv.push_back(const_cast<char*>(s.c_str()));
        CliOptions o2;
        std::string e2;
        CHECK(!parse_cli(static_cast<int>(bad_argv.size()), bad_argv.data(), o2, e2),
              "parse_cli rejects an unreadable list file");
        CHECK(!e2.empty(), "unreadable list file reports an error");

        std::filesystem::remove_all(dir, ec);
    }

    if (!std::filesystem::exists(g_tool_path)) {
        printf("  (skipping file-list tests: %s not built)\n", g_tool_path.c_str());
        return;
    }

    std::vector<uint8_t> rgb;
    make_synthetic_rgb(160, 120, rgb);
    std::vector<uint8_t> jpeg;
    std::string err;
    CHECK(compress_rgb(rgb, 160, 120, 85, false, jpeg, err), "list fixture compress");

    std::string dir = (std::filesystem::temp_directory_path() /
                       ("jpegopt-list-" + std::to_string(getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);
    std::string a = dir + "/a.jpg";
    std::string b = dir + "/b.jpg";
    std::string missing = dir + "/ghost.jpg";
    CHECK(util::write_file(a, jpeg, err), "write a.jpg");
    CHECK(util::write_file(b, jpeg, err), "write b.jpg");

    std::string out;

    // --files-from: one path per line; blank lines and '#' comments ignored.
    std::string list_path = dir + "/list.txt";
    std::string list = "# leading comment\n" + a + "\n\n   \n" + b + "\n# trailing\n";
    CHECK(util::write_file(list_path, std::vector<uint8_t>(list.begin(), list.end()), err),
          "write list file");
    CHECK(run_tool("--dry-run --files-from " + shell_quote(list_path), out) == 0,
          "--files-from exits 0");
    CHECK(out.find("a.jpg") != std::string::npos && out.find("b.jpg") != std::string::npos,
          "--files-from processed both listed files");
    CHECK(out.find("Total: 2 file(s)") != std::string::npos,
          "--files-from listed exactly two files");

    // --files-stdin: same grammar, read from stdin.
    std::string stdin_list = "# comment\n" + a + "\n\n" + b + "\n";
    CHECK(run_tool_stdin("--dry-run --files-stdin", stdin_list, out) == 0,
          "--files-stdin exits 0");
    CHECK(out.find("a.jpg") != std::string::npos && out.find("b.jpg") != std::string::npos,
          "--files-stdin processed both listed files");

    // An unreadable list is a usage error (exit 2), not a crash.
    CHECK(run_tool("--dry-run --files-from " + shell_quote(dir + "/nope.txt"), out) == 2,
          "--files-from missing file exits 2");

    // A listed path that does not exist is skipped (not an error), so a single
    // good entry still yields a successful run.
    std::string partial = dir + "/partial.txt";
    std::string plist = a + "\n" + missing + "\n";
    CHECK(util::write_file(partial, std::vector<uint8_t>(plist.begin(), plist.end()), err),
          "write partial list");
    CHECK(run_tool("--dry-run --files-from " + shell_quote(partial), out) == 0,
          "missing listed path is skipped, exit 0");
    CHECK(out.find("Total: 1 file(s)") != std::string::npos,
          "only the existing listed file is processed");

    std::filesystem::remove_all(dir, ec);
}

void test_cli_contract_extras() {
    // -t / --threads validation and clamping (reported on the wire, not just in
    // the parser) plus the reporting contracts that changed: method is empty
    // when nothing was selected, and stripped_markers is populated under
    // --strip-metadata.
    if (!std::filesystem::exists(g_tool_path)) {
        printf("  (skipping CLI contract extras: %s not built)\n", g_tool_path.c_str());
        return;
    }

    std::vector<uint8_t> rgb;
    make_synthetic_rgb(160, 120, rgb);
    std::vector<uint8_t> jpeg;
    std::string err;
    CHECK(compress_rgb(rgb, 160, 120, 85, false, jpeg, err), "extras fixture compress");

    std::string dir = (std::filesystem::temp_directory_path() /
                       ("jpegopt-extras-" + std::to_string(getpid()))).string();
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);
    std::string src = dir + "/src.jpg";
    CHECK(util::write_file(src, jpeg, err), "write extras fixture");

    std::string out;

    // Invalid thread counts are usage errors; 0 and values above the cap are
    // accepted (0 = auto, above the cap = clamped).
    CHECK(run_tool("--dry-run -t abc " + shell_quote(src), out) == 2,
          "-t abc exits 2");
    CHECK(run_tool("--dry-run -t 2x " + shell_quote(src), out) == 2,
          "-t 2x exits 2");
    CHECK(run_tool("--dry-run -t -3 " + shell_quote(src), out) == 2,
          "-t -3 exits 2");
    CHECK(run_tool("--dry-run -t 0 " + shell_quote(src), out) == 0,
          "-t 0 (auto) exits 0");
    CHECK(run_tool("--dry-run -t 999 " + shell_quote(src), out) == 0,
          "-t above the cap is clamped, not rejected");
    CHECK(run_tool("--dry-run --threads 8 " + shell_quote(src), out) == 0,
          "--threads 8 exits 0");

    // method is empty exactly when nothing was selected. Running twice in place
    // converges, so the second pass has no smaller candidate to select.
    CHECK(run_tool("-i " + shell_quote(src), out) == 0, "first optimize exit 0");
    CHECK(run_tool("-i " + shell_quote(src), out) == 0, "second optimize exit 0");
    {
        std::string cmd = "--dry-run --json " + shell_quote(src);
        CHECK(run_tool(cmd, out) == 0, "json run exit 0");
        std::vector<std::string> objs;
        find_top_level_objects(out, objs);
        CHECK(objs.size() == 1, "extras: one json object");
        if (!objs.empty()) {
            const std::string& o = objs[0];
            if (o.find("\"changed\":false") != std::string::npos) {
                CHECK(o.find("\"method\":\"\"") != std::string::npos,
                      "method is empty when nothing was selected");
                CHECK(o.find("\"arith\":false") != std::string::npos,
                      "arith is false when nothing was selected");
            }
        }
    }

    // stripped_markers is populated under --strip-metadata: it lists the
    // source's strippable categories instead of staying empty.
    {
        std::vector<uint8_t> meta;
        CHECK(compress_rgb_fixture(rgb, 160, 120, 85, 0, true, meta, err),
              "extras metadata fixture");
        std::string mpath = dir + "/meta.jpg";
        CHECK(util::write_file(mpath, meta, err), "write extras metadata");
        CHECK(run_tool("--dry-run --strip-metadata --json " + shell_quote(mpath), out) == 0,
              "strip-metadata json exit 0");
        std::vector<std::string> objs;
        find_top_level_objects(out, objs);
        CHECK(objs.size() == 1, "strip-metadata: one json object");
        if (!objs.empty()) {
            CHECK(objs[0].find("\"strip_metadata\":true") != std::string::npos,
                  "strip_metadata flag reported");
            CHECK(objs[0].find("\"stripped_markers\":\"\"") == std::string::npos,
                  "--strip-metadata reports the categories it removes");
            CHECK(objs[0].find("exif") != std::string::npos,
                  "--strip-metadata lists exif");
        }
    }

    std::filesystem::remove_all(dir, ec);
}

void test_reader_regressions() {
    // Direct unit checks for three defects found by review:
    //  - a bare relative output name must not send the temp file to the
    //    filesystem root (write_atomic / temp_name);
    //  - DHT (0xC4) and JPG (0xC8) are inside the numeric 0xC0..0xCB span but
    //    are not frame headers, so they must not satisfy read_jpeg_info;
    //  - extract_scan_script must not read past the end of a truncated SOS.

    // 1. write_atomic with a bare relative target.
    {
        std::string dir = (std::filesystem::temp_directory_path() /
                           ("jpegopt-relp-" + std::to_string(getpid()))).string();
        std::error_code ec;
        std::filesystem::create_directory(dir, ec);
        std::string cwd = std::filesystem::current_path().string();
        std::filesystem::current_path(dir);
        std::vector<uint8_t> payload = {'o', 'k'};
        std::string werr;
        bool wrote = util::write_atomic("bare.out", payload, werr, "", "");
        std::filesystem::current_path(cwd);
        CHECK(wrote, ("write_atomic with a bare relative name: " + werr).c_str());
        CHECK(util::file_exists(dir + "/bare.out"), "bare relative output landed in cwd");
        CHECK(!util::file_exists("/.jpegopt.tmp.probe"), "no temp file at the filesystem root");
        std::filesystem::remove_all(dir, ec);
    }

    // 2. DHT-only input is not a frame.
    {
        std::vector<uint8_t> d = {0xFF, 0xD8};
        std::vector<uint8_t> seg = {0xFF, 0xC4, 0x00, 0x0E,
                                    8, 0, 10, 0, 10, 1, 2, 3, 4, 5, 6};
        d.insert(d.end(), seg.begin(), seg.end());
        d.push_back(0xFF);
        d.push_back(0xD9);
        JpegInfo i = read_jpeg_info(d);
        CHECK(!i.valid, "DHT-only input is not a valid frame");
        CHECK(!i.is_baseline, "DHT does not report baseline");
    }

    // 3. Real file whose DHT precedes its SOF: dimensions must match the frame,
    //    not the Huffman table segment.
    {
        std::vector<uint8_t> rgb;
        make_synthetic_rgb(200, 120, rgb);
        std::vector<uint8_t> jpeg;
        std::string err;
        CHECK(compress_rgb(rgb, 200, 120, 85, false, jpeg, err), "regression fixture compress");
        {
            FILE* f = fopen((std::filesystem::temp_directory_path() /
                             ("jpegopt-dht-" + std::to_string(getpid()) + ".jpg")).string().c_str(),
                            "wb");
            if (f) {
                fwrite(jpeg.data(), 1, jpeg.size(), f);
                fclose(f);
            }
        }
        JpegInfo i = read_jpeg_info(jpeg);
        CHECK(i.valid, "fixture is valid");
        CHECK(i.width == 200 && i.height == 120,
              "frame dimensions come from SOF, not from a preceding DHT");
        CHECK(i.num_components == 3, "component count from SOF");
    }

    // 4. Truncated SOS: extract_scan_script must refuse, not over-read.
    {
        std::vector<uint8_t> d = {0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x02};
        std::vector<JpegScan> scans;
        CHECK(!extract_scan_script(d, scans), "truncated SOS is rejected");
        CHECK(scans.empty(), "no scan recorded from a truncated SOS");
    }
}

}  // namespace

// Sets the path of the CLI binary the exit-code tests drive. The test binary is
// built into the same directory as the tool, so argv[0]'s directory is where
// jpegopt lives.
void set_tool_path(const char* argv0) {
    std::filesystem::path p = argv0 ? std::filesystem::path(argv0) : std::filesystem::path();
    std::filesystem::path dir = p.has_parent_path() ? p.parent_path() : std::filesystem::path("build");
    g_tool_path = (dir / "jpegopt").string();
}

int main(int argc, char** argv) {
    set_tool_path(argc > 0 ? argv[0] : nullptr);
    (void)argc;

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

    test_marker_strip_unit();
    test_pipeline_markers();
    test_pipeline_threshold();
    test_json_schema();
    test_edge_inputs();
    test_cmyk_input();
    test_twelve_bit_input();
    test_file_lists();
    test_cli_exit_codes();
    test_cli_contract_extras();
    test_reader_regressions();

    if (failures == 0) {
        printf("All tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test(s) failed.\n", failures);
    return 1;
}
