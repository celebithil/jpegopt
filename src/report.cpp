// SPDX-License-Identifier: Apache-2.0

#include "report.h"

#include <cstdio>
#include <string>

#include "util.h"

namespace {

std::string method_suffix(const FileResult& r) {
    if (r.method.empty()) return "";
    std::string s = " [" + r.method + "]";
    if (r.arith_used) s += " [arith]";
    if (r.arith_fell_back) s += " [arith->huff]";
    return s;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

}  // namespace

void build_report(const std::vector<FileResult>& results, bool json, bool show_all,
                  std::string& out) {
    if (json) {
        out = "[\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const FileResult& r = results[i];
            out += "  {\"path\":\"" + json_escape(r.path) + "\",\"ok\":" +
                   (r.ok ? "true" : "false") + ",\"changed\":" +
                   (r.changed ? "true" : "false") + ",\"strip_metadata\":" +
                   (r.strip_metadata ? "true" : "false") + ",\"original_size\":" +
                   std::to_string(r.original_size) + ",\"best_size\":" +
                   std::to_string(r.best_size) + ",\"method\":\"" +
                   json_escape(r.method) + "\",\"arith\":" +
                   (r.arith_used ? "true" : "false") + ",\"arith_fell_back\":" +
                   (r.arith_fell_back ? "true" : "false") + ",\"error\":\"" +
                   json_escape(r.error) + "\",\"candidates\":{";
            for (size_t c = 0; c < r.candidates.size(); ++c) {
                if (c) out += ",";
                out += "\"" + json_escape(r.candidates[c].method) + "\":" +
                       std::to_string(r.candidates[c].size);
            }
            out += "}}";
            out += (i + 1 < results.size()) ? ",\n" : "\n";
        }
        out += "]\n";
        return;
    }

    uint64_t total_before = 0;
    uint64_t total_after = 0;
    size_t changed_count = 0;
    size_t error_count = 0;

    for (const FileResult& r : results) {
        if (!r.ok) {
            ++error_count;
            char line[8192];
            snprintf(line, sizeof(line), "%s: ERROR: %s\n", r.path.c_str(),
                     r.error.c_str());
            out += line;
            continue;
        }
        total_before += r.original_size;
        total_after += r.best_size;
        if (r.changed) ++changed_count;
        if (r.original_size == 0) continue;

        double ratio = static_cast<double>(r.best_size) / static_cast<double>(r.original_size);
        char line[8192];
        if (r.changed) {
            snprintf(line, sizeof(line), "%s: %s -> %s (-%s)%s\n", r.path.c_str(),
                     util::format_bytes(r.original_size).c_str(),
                     util::format_bytes(r.best_size).c_str(),
                     util::format_pct(ratio).c_str(), method_suffix(r).c_str());
        } else {
            snprintf(line, sizeof(line), "%s: %s (unchanged)\n", r.path.c_str(),
                     util::format_bytes(r.original_size).c_str());
        }
        out += line;
        if (show_all) {
            for (const auto& c : r.candidates) {
                double c_ratio =
                    static_cast<double>(c.size) / static_cast<double>(r.original_size);
                snprintf(line, sizeof(line), "    %-16s %8zu B (%s)%s\n",
                         c.method.c_str(), c.size, util::format_pct(c_ratio).c_str(),
                         c.arith ? " [arith]" : "");
                out += line;
            }
        }
    }

    char summary[2048];
    const bool strip = !results.empty() && results[0].strip_metadata;
    if (total_before > 0) {
        double ratio = static_cast<double>(total_after) / static_cast<double>(total_before);
        snprintf(summary, sizeof(summary),
                 "Total: %zu file(s), %zu changed, %zu error(s); %s -> %s (-%s)%s\n",
                 results.size(), changed_count, error_count,
                 util::format_bytes(total_before).c_str(),
                 util::format_bytes(total_after).c_str(),
                 util::format_pct(ratio).c_str(),
                 strip ? " (metadata stripped)" : "");
    } else {
        snprintf(summary, sizeof(summary),
                 "Total: %zu file(s), %zu changed, %zu error(s)\n", results.size(),
                 changed_count, error_count);
    }
    out += summary;
}
