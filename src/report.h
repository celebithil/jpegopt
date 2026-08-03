// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "pipeline.h"

// Writes per-file lines plus a summary. With `json` true, emits a single JSON
// document to `out` and no human text. With `show_all` true (text mode only),
// prints every verified candidate size beneath each file line.
void build_report(const std::vector<FileResult>& results, bool json, bool show_all,
                  std::string& out);
