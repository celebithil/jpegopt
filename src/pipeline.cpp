// SPDX-License-Identifier: Apache-2.0

#include "pipeline.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <vector>

#include "guetzli_encode.h"
#include "jpeg_reader.h"
#include "transcoder.h"
#include "util.h"
#include "verify.h"

namespace {

struct Candidate {
    std::string method;
    bool arith = false;
    std::vector<uint8_t> bytes;
};

bool verify_candidate(const DecodedImage& reference,
                      const std::vector<uint8_t>& candidate_bytes, std::string& err) {
    DecodedImage cand;
    if (!decode_native(candidate_bytes, cand, err)) {
        return false;
    }
    if (!same_image(reference, cand)) {
        err = "decoded pixels differ from original";
        return false;
    }
    return true;
}

void add_if_valid(Candidate&& cand, const DecodedImage& reference,
                  std::vector<Candidate>& out) {
    if (cand.bytes.empty()) return;
    std::string err;
    if (!verify_candidate(reference, cand.bytes, err)) {
        return;
    }
    out.push_back(std::move(cand));
}

// Applies the granular marker policy to encoded bytes. The --strip-metadata
// fast path (markers never copied) needs no post-processing.
void maybe_strip(std::vector<uint8_t>& bytes, const PipelineOptions& opts) {
    if (!opts.strip_metadata && opts.markers.strips_anything()) {
        strip_markers(bytes, opts.markers);
    }
}

// --verbose diagnostics go to stderr so that stdout stays machine-readable
// (--json). Serialised because workers run concurrently.
void log_verbose(const PipelineOptions& opts, const std::string& line) {
    if (!opts.verbose) return;
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    fprintf(stderr, "[jpegopt] %s\n", line.c_str());
}

}  // namespace


FileResult process_file(const std::string& path, const PipelineOptions& opts) {
    FileResult res;
    res.path = path;
    const auto started = std::chrono::steady_clock::now();

    std::vector<uint8_t> original;
    std::string err;
    if (!util::read_file(path, original, err)) {
        res.ok = false;
        res.error = err;
        log_verbose(opts, path + ": ERROR " + err);
        return res;
    }
    res.original_size = original.size();

    JpegInfo info = read_jpeg_info(original);
    if (!info.valid) {
        res.ok = false;
        res.error = info.error;
        log_verbose(opts, path + ": ERROR " + info.error);
        return res;
    }

    DecodedImage reference;
    if (!decode_native(original, reference, err)) {
        res.ok = false;
        res.error = "cannot decode source: " + err;
        log_verbose(opts, path + ": ERROR cannot decode source: " + err);
        return res;
    }

    // 12-bit sources cannot go through the jpeglib coefficient path (the
    // vendored build has no 12-bit coefficient API), so they take a separate
    // TurboJPEG transform route: still lossless, but limited to a single
    // progressive-Huffman candidate.
    const bool high_precision = reference.bytes_per_sample > 1;

    std::string temp_dir;
    if (!util::make_temp_dir(opts.temp_dir, temp_dir, err)) {
        res.ok = false;
        res.error = err;
        log_verbose(opts, path + ": ERROR " + err);
        return res;
    }

    std::vector<Candidate> candidates;

    // C2: progressive Huffman variants across the scan-script family
    // (retained: variants that won >=1 file in the huff-only default mode).
    const struct {
        ScanStyle style;
        const char* tag;
    } huff_styles[] = {
        {ScanStyle::kLibJpeg, "progressive"},
        {ScanStyle::kFewScan, "progressive-min"},
        {ScanStyle::kFineBands, "progressive-bands"},
        {ScanStyle::kSuperFine, "progressive-fine"},
        {ScanStyle::kFewScanAl3, "progressive-al3"},
        {ScanStyle::kMozMax, "progressive-mozmax"},
        {ScanStyle::kFewScanAl1DcPlain, "progressive-dcplain"},
        {ScanStyle::kFewScanAl2DcPlain, "progressive-al2-dcplain"},
        {ScanStyle::kFewScanAl3DcPlain, "progressive-al3-dcplain"},
        {ScanStyle::kFewScanAl4DcPlain, "progressive-al4-dcplain"},
        {ScanStyle::kMozMaxDcSa1, "progressive-mozmax-dcsa"},
        {ScanStyle::kMozMaxSplit5, "progressive-mozmax-split5"},
        {ScanStyle::kMozMaxChromaSa, "progressive-mozmax-chromasa"},
        {ScanStyle::kMozMaxAl3, "progressive-mozmax-al3"},
        {ScanStyle::kFewScanAl5DcPlain, "progressive-al5-dcplain"},
        {ScanStyle::kFewScanAl6DcPlain, "progressive-al6-dcplain"},
        {ScanStyle::kMozMaxAl4, "progressive-mozmax-al4"},
        {ScanStyle::kMozMaxSplit5Al3, "progressive-mozmax-split5-al3"},
        {ScanStyle::kMozMaxAl3ChromaSa, "progressive-mozmax-al3-chromasa"},
        {ScanStyle::kFineBandsDcPlain, "progressive-bands-dcplain"},
        {ScanStyle::kMediumBandsDcPlain, "progressive-medium-dcplain"},
        {ScanStyle::kMozDefault, "progressive-moz-default"},
        {ScanStyle::kMozFast, "progressive-moz-fast"},
    };
    // C4b: progressive arithmetic variants across the scan-script family
    // (retained: variants that won >=1 file in the arith mode).
    const struct {
        ScanStyle style;
        const char* tag;
    } arith_styles[] = {
        {ScanStyle::kLibJpeg, "progressive"},
        {ScanStyle::kFewScan, "progressive-min"},
        {ScanStyle::kFewScanAl2, "progressive-al2"},
        {ScanStyle::kFewScanAl3, "progressive-al3"},
        {ScanStyle::kFewScanAl4, "progressive-al4"},
        {ScanStyle::kMozMax, "progressive-mozmax"},
        {ScanStyle::kFewScanAl1DcPlain, "progressive-dcplain"},
        {ScanStyle::kFewScanAl2DcPlain, "progressive-al2-dcplain"},
        {ScanStyle::kFewScanAl3DcPlain, "progressive-al3-dcplain"},
        {ScanStyle::kFewScanAl4DcPlain, "progressive-al4-dcplain"},
        {ScanStyle::kMozMaxAl3, "progressive-mozmax-al3"},
        {ScanStyle::kFewScanAl5DcPlain, "progressive-al5-dcplain"},
        {ScanStyle::kFewScanAl6DcPlain, "progressive-al6-dcplain"},
        {ScanStyle::kMozMaxAl4, "progressive-mozmax-al4"},
        {ScanStyle::kMozMaxSplit5Al3, "progressive-mozmax-split5-al3"},
        {ScanStyle::kMozMaxAl3ChromaSa, "progressive-mozmax-al3-chromasa"},
        {ScanStyle::kMediumBandsAl2, "progressive-medium-al2"},
        {ScanStyle::kMozDefault, "progressive-moz-default"},
        {ScanStyle::kSuperFineAl2, "progressive-superfine-al2"},
        {ScanStyle::kSuperFineAl3, "progressive-superfine-al3"},
    };
    auto add_progressive = [&](ScanStyle style, const char* tag, bool arith) {
        std::vector<uint8_t> t;
        std::string terr;
        TranscodeOptions t2;
        t2.progressive = true;
        t2.arith = arith;
        t2.optimize = true;
        t2.strip_restart = true;
        t2.strip_metadata = opts.strip_metadata;
        t2.scan_style = style;
        if (lossless_transcode(original, t2, t, terr)) {
            maybe_strip(t, opts);
            add_if_valid(Candidate{tag, arith, std::move(t)}, reference,
                         candidates);
        }
    };
    for (const auto& s : huff_styles) {
        if (!high_precision) add_progressive(s.style, s.tag, false);
    }

    // 12-bit route: one progressive-Huffman candidate via TurboJPEG. The
    // scan-script family and arithmetic coding are unavailable here, so this is
    // the only candidate such a source can produce.
    if (high_precision) {
        std::vector<uint8_t> t;
        std::string terr;
        if (lossless_transcode_tj(original, opts.strip_metadata, t, terr)) {
            maybe_strip(t, opts);
            add_if_valid(Candidate{"progressive-12bit", false, std::move(t)}, reference,
                         candidates);
        }
    }

    // C3: guetzli sequential encoder with cost-clustered Huffman tables
    // (an independent, fully different re-encode path). 12-bit sources are
    // rejected by guetzli's reader, so it is skipped for them.
    if (!high_precision) {
        std::vector<uint8_t> t;
        std::string terr;
        if (guetzli_encode(original, opts.strip_metadata, t, terr)) {
            maybe_strip(t, opts);
            add_if_valid(Candidate{"guetzli", false, std::move(t)}, reference,
                         candidates);
        }
    }

    if (opts.allow_arith && !high_precision) {
        // C4a: sequential arithmetic coding.
        {
            std::vector<uint8_t> t;
            std::string terr;
            TranscodeOptions t2;
            t2.arith = true;
            t2.optimize = true;
            t2.strip_restart = true;
            t2.strip_metadata = opts.strip_metadata;
            if (lossless_transcode(original, t2, t, terr)) {
                maybe_strip(t, opts);
                add_if_valid(Candidate{"arith", true, std::move(t)}, reference,
                             candidates);
            }
        }
        // C4b: progressive arithmetic coding across the scan-script family.
        for (const auto& s : arith_styles) {
            std::string tag = "progressive+arith";
            std::string huff_tag = s.tag;
            const std::string prefix = "progressive";
            if (huff_tag != prefix) {
                tag += huff_tag.substr(prefix.size());
            }
            add_progressive(s.style, tag.c_str(), true);
        }
    }

    // Pick the best candidate, applying the AC-fallback rule:
    // arithmetic output is used only when it is strictly smaller than the best
    // Huffman-based result; otherwise we re-encode without AC.
    for (const auto& c : candidates) {
        res.candidates.push_back(FileResult::CandidateInfo{c.method, c.bytes.size(), c.arith});
    }
    if (opts.verbose) {
        char buf[256];
        const int expected = high_precision ? 1 : (opts.allow_arith ? 45 : 24);
        snprintf(buf, sizeof(buf), "%s: %zu/%d candidates verified",
                 path.c_str(), candidates.size(), expected);
        log_verbose(opts, buf);
    }

    Candidate* best_huff = nullptr;
    Candidate* best_ac = nullptr;
    for (auto& c : candidates) {
        if (c.arith) {
            if (!best_ac || c.bytes.size() < best_ac->bytes.size()) best_ac = &c;
        } else {
            if (!best_huff || c.bytes.size() < best_huff->bytes.size()) best_huff = &c;
        }
    }

    const Candidate* winner = nullptr;
    bool arith_used = false;
    if (best_ac && (!best_huff || best_ac->bytes.size() < best_huff->bytes.size())) {
        winner = best_ac;
        arith_used = true;
    } else {
        winner = best_huff;
    }
    if (best_ac && best_huff && best_ac->bytes.size() >= best_huff->bytes.size()) {
        res.arith_fell_back = true;
    }

    res.best_size = res.original_size;
    res.strip_metadata = opts.strip_metadata;
    MarkerStripped stripped = classify_strippable(
        original, opts.strip_metadata ? MarkerPolicy::all() : opts.markers);
    if (stripped.any()) {
        res.stripped_markers = stripped.summary();
    }
    if (winner && winner->bytes.size() < res.original_size) {
        res.method = winner->method;
        res.arith_used = arith_used;
    }
    if (winner) {
        if (winner->bytes.size() < res.original_size) {
            const double savings_pct =
                100.0 * (1.0 - static_cast<double>(winner->bytes.size()) /
                                   static_cast<double>(res.original_size));
            if (savings_pct >= opts.min_savings_pct) {
                res.best_size = winner->bytes.size();
                res.changed = true;
                res.best_bytes = winner->bytes;

                if (!opts.dry_run) {
                    std::string out_path = opts.in_place ? path : util::output_path_for(path);
                    const std::string preserve_from =
                        (opts.preserve && !opts.in_place) ? path : "";
                    if (!util::write_atomic(out_path, winner->bytes, err, preserve_from,
                                            temp_dir)) {
                        res.ok = false;
                        res.error = err;
                        log_verbose(opts, path + ": ERROR " + err);
                        util::remove_all(temp_dir);
                        return res;
                    }
                    log_verbose(opts, path + ": wrote " + out_path);
                }
            } else {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s: kept original, %.2f%% < threshold %.2f%%",
                         path.c_str(), savings_pct, opts.min_savings_pct);
                log_verbose(opts, buf);
            }
        }
    }

    res.ok = true;
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started)
                            .count();
        char buf[512];
        if (res.changed) {
            snprintf(buf, sizeof(buf), "%s: %llu -> %llu (-%.2f%%) via %s in %lldms%s",
                     path.c_str(), static_cast<unsigned long long>(res.original_size),
                     static_cast<unsigned long long>(res.best_size),
                     res.original_size
                         ? 100.0 * (1.0 - static_cast<double>(res.best_size) /
                                             static_cast<double>(res.original_size))
                         : 0.0,
                     res.method.c_str(),
                     static_cast<long long>(ms),
                     res.arith_fell_back ? " [arith->huff]" : "");
        } else if (!res.method.empty()) {
            // A smaller candidate existed but the threshold blocked the write;
            // the reason was already logged above, so do not claim it as a win.
            snprintf(buf, sizeof(buf),
                     "%s: kept original (%llu B), below threshold in %lldms",
                     path.c_str(), static_cast<unsigned long long>(res.original_size),
                     static_cast<long long>(ms));
        } else {
            snprintf(buf, sizeof(buf),
                     "%s: kept original (%llu B), no smaller candidate in %lldms",
                     path.c_str(), static_cast<unsigned long long>(res.original_size),
                     static_cast<long long>(ms));
        }
        log_verbose(opts, buf);
    }
    util::remove_all(temp_dir);
    return res;
}
