// SPDX-License-Identifier: Apache-2.0

#include "pipeline.h"

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
    if (!decode_rgb(candidate_bytes, cand, err)) {
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

}  // namespace

FileResult process_file(const std::string& path, const PipelineOptions& opts) {
    FileResult res;
    res.path = path;

    std::vector<uint8_t> original;
    std::string err;
    if (!util::read_file(path, original, err)) {
        res.ok = false;
        res.error = err;
        return res;
    }
    res.original_size = original.size();

    JpegInfo info = read_jpeg_info(original);
    if (!info.valid) {
        res.ok = false;
        res.error = info.error;
        return res;
    }

    DecodedImage reference;
    if (!decode_rgb(original, reference, err)) {
        res.ok = false;
        res.error = "cannot decode source: " + err;
        return res;
    }

    std::string temp_dir;
    if (!util::make_temp_dir(opts.temp_dir, temp_dir, err)) {
        res.ok = false;
        res.error = err;
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
            add_if_valid(Candidate{tag, arith, std::move(t)}, reference,
                         candidates);
        }
    };
    for (const auto& s : huff_styles) {
        add_progressive(s.style, s.tag, false);
    }

    // C3: guetzli sequential encoder with cost-clustered Huffman tables
    // (an independent, fully different re-encode path).
    {
        std::vector<uint8_t> t;
        std::string terr;
        if (guetzli_encode(original, opts.strip_metadata, t, terr)) {
            add_if_valid(Candidate{"guetzli", false, std::move(t)}, reference,
                         candidates);
        }
    }

    if (opts.allow_arith) {
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
    if (winner) {
        res.method = winner->method;
        res.arith_used = arith_used;
        if (winner->bytes.size() < res.original_size) {
            res.best_size = winner->bytes.size();
            res.changed = true;
            res.best_bytes = winner->bytes;

            if (!opts.dry_run) {
                std::string out_path = opts.in_place ? path : util::output_path_for(path);
                if (!util::write_atomic(out_path, winner->bytes, err)) {
                    res.ok = false;
                    res.error = err;
                    util::remove_all(temp_dir);
                    return res;
                }
            }
        }
    }

    res.ok = true;
    util::remove_all(temp_dir);
    return res;
}
