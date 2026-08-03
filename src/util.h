// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace util {

bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string& err);
bool write_file(const std::string& path, const std::vector<uint8_t>& data, std::string& err);
// Writes data to a unique temp file next to `target`, then renames over it.
bool write_atomic(const std::string& target, const std::vector<uint8_t>& data, std::string& err);
bool file_exists(const std::string& path);
uint64_t file_size(const std::string& path);

// Returns the path of the running executable (readlink /proc/self/exe).
std::string self_path();

std::string format_bytes(uint64_t bytes);
std::string format_pct(double ratio);

// Creates a unique temporary directory under base (or system temp if empty).
bool make_temp_dir(const std::string& base, std::string& out_dir, std::string& err);
bool remove_all(const std::string& dir);

// Replaces the last extension of a filename (e.g. photo.jpg -> photo.opt.jpg).
std::string output_path_for(const std::string& input_path);

}  // namespace util
