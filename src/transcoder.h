// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jpeglib.h"

// Progressive scan script selection for the transcode candidates.
enum class ScanStyle {
    kLibJpeg,     // libjpeg-turbo's jpeg_simple_progression (default)
    kFewScan,     // DC + one AC band per component, two successive passes
    kFineBands,   // DC + fine AC band splits (1-4/5-8/9-16/17-32/33-63)
    kMediumBands, // DC + medium AC band splits (1-8/9-16/17-31/32-63)
    kSuperFine,   // DC + very fine AC band splits (1-3/4-6/7-10/11-16/17-24/25-32/33-63)
    kFewScanAl2,  // kFewScan band structure, first pass Al=2 (2 refinements)
    kFewScanAl3,  // kFewScan band structure, first pass Al=3 (3 refinements)
    kFewScanAl4,  // kFewScan band structure, first pass Al=4 (4 refinements)
    kMozMax,      // mozjpeg's max-compression script (DC without SA, Al=2 luma)
    // Plain-DC family: DC in a single (0,0) scan, AC from the named family.
    kFewScanAl1DcPlain,
    kFewScanAl2DcPlain,
    kFewScanAl3DcPlain,
    kFewScanAl4DcPlain,
    // mozjpeg-structure decomposition variants.
    kMozMaxDcSa1,     // DC with successive approximation (0,1)->(1,0)
    kMozMaxSplit5,    // luma AC split 1-5/6-63 instead of 1-8/9-63
    kMozMaxChromaSa,  // chroma AC with successive approximation (Al=1)
    kMozMaxAl3,       // luma AC first pass Al=3
    kFewScanAl5DcPlain,  // kFewScanAl4DcPlain with first pass Al=5
    kFewScanAl6DcPlain,  // kFewScanAl4DcPlain with first pass Al=6
    kMozMaxAl4,          // mozjpeg structure, luma AC first pass Al=4
    kMozMaxSplit5Al3,    // mozjpeg structure, luma split 1-5/6-63 and Al=3
    kMozMaxAl3ChromaSa,  // mozjpeg structure, luma Al=3 and chroma SA
    // Fine/medium band families with a plain DC scan.
    kFineBandsDcPlain,    // kFineBands structure, DC as a single (0,0) scan
    kMediumBandsDcPlain,  // kMediumBands structure, DC as a single (0,0) scan
    // kMediumBands with a successive-approximation first pass.
    kMediumBandsAl2,
    // mozjpeg's JCP_DEFAULT script (separate luma/chroma DC scans, no SA).
    kMozDefault,
    // mozjpeg's JCP_FAST script (3 interleaved scans, no SA).
    kMozFast,
    // Super-fine band family with deeper successive-approximation chains.
    kSuperFineAl2,
    kSuperFineAl2DcPlain,
    kSuperFineAl3,
    kSuperFineAl3DcPlain,
};

struct TranscodeOptions {
    bool progressive = false;
    bool arith = false;
    bool optimize = false;
    // Drop DRI/RSTn restart markers from the output (pixels unchanged).
    bool strip_restart = false;
    // Drop APP/COM markers (JFIF/EXIF/comments) from the output. Pixels
    // unchanged; metadata is lost.
    bool strip_metadata = false;
    // Only meaningful when progressive is true.
    ScanStyle scan_style = ScanStyle::kLibJpeg;
    // Optional explicit scan script (as built by extract_scan_script from a
    // progressive source). When non-null and non-empty it overrides
    // scan_style, reproducing the source's own band structure.
    const std::vector<jpeg_scan_info>* scan_override = nullptr;
};

// Losslessly re-encodes the DCT coefficients of `src` (jpegtran-style:
// no geometric transform, so it is always "perfect"). APP/COM markers are
// preserved unless strip_metadata is set. Returns false on decode/encode
// failure.
bool lossless_transcode(const std::vector<uint8_t>& src, const TranscodeOptions& opt,
                        std::vector<uint8_t>& out, std::string& err);

// Lossless transcode for sources the jpeglib coefficient path cannot handle,
// notably 12-bit-per-sample JPEG. Uses TurboJPEG's tj3Transform, which rewrites
// the entropy coding (progressive + optimized Huffman) while keeping the DCT
// coefficients, precision and colour model untouched. Only the
// precision-preserving options are honoured; arithmetic coding is not applied.
bool lossless_transcode_tj(const std::vector<uint8_t>& src, bool strip_metadata,
                           std::vector<uint8_t>& out, std::string& err);
