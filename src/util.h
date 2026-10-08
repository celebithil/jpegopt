// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace util {

bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string& err);
bool write_file(const std::string& path, const std::vector<uint8_t>& data, std::string& err);
// Writes data to a unique temp file, then moves it onto `target`.
// The mode and timestamps of an existing `target` are preserved. When `target`
// does not exist and `preserve_from` is non-empty, the mode/timestamps of that
// file are copied instead (used by --preserve for fresh <name>.opt.<ext>).
//
// `stage_dir` selects where the temp file is created. When empty (the default)
// the temp file is created next to `target`, so the final move is a single
// atomic rename. When set (--temp-dir), the data is staged there first; it is
// then renamed into place if it lives on the same filesystem, or copied to a
// temp file beside `target` and renamed otherwise.
bool write_atomic(const std::string& target, const std::vector<uint8_t>& data,
                  std::string& err, const std::string& preserve_from = "",
                  const std::string& stage_dir = "");
// True when both paths live on the same filesystem, so rename(2) can move a
// file between them atomically. Unreadable paths yield false.
bool same_filesystem(const std::string& a, const std::string& b);
bool file_exists(const std::string& path);

std::string format_bytes(uint64_t bytes);
std::string format_pct(double ratio);

// Creates a unique temporary directory under base (or system temp if empty).
bool make_temp_dir(const std::string& base, std::string& out_dir, std::string& err);
bool remove_all(const std::string& dir);

// Replaces the last extension of a filename (e.g. photo.jpg -> photo.opt.jpg).
std::string output_path_for(const std::string& input_path);

}  // namespace util
