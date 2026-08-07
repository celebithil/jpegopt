// SPDX-License-Identifier: Apache-2.0

#include "util.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>

namespace util {

bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        err = "cannot open " + path;
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        err = "cannot seek " + path;
        return false;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        err = "cannot tell " + path;
        return false;
    }
    fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(size));
    if (!out.empty() && fread(out.data(), 1, out.size(), f) != out.size()) {
        fclose(f);
        err = "cannot read " + path;
        return false;
    }
    fclose(f);
    return true;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& data, std::string& err) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        err = "cannot open for writing " + path;
        return false;
    }
    if (!data.empty() && fwrite(data.data(), 1, data.size(), f) != data.size()) {
        fclose(f);
        err = "cannot write " + path;
        return false;
    }
    if (fclose(f) != 0) {
        err = "cannot close " + path;
        return false;
    }
    return true;
}

bool write_atomic(const std::string& target, const std::vector<uint8_t>& data, std::string& err,
                  const std::string& preserve_from) {
    std::filesystem::path tp(target);
    std::string tmp = tp.parent_path().string() + "/.jpegopt.tmp." +
                      std::to_string(static_cast<long long>(getpid())) + "." +
                      std::to_string(static_cast<long long>(time(nullptr)));
    if (!write_file(tmp, data, err)) {
        return false;
    }
    struct stat st;
    bool have = stat(target.c_str(), &st) == 0;
    if (!have && !preserve_from.empty()) {
        have = stat(preserve_from.c_str(), &st) == 0;
    }
    if (have) {
        chmod(tmp.c_str(), st.st_mode & 07777);
    }
    if (rename(tmp.c_str(), target.c_str()) != 0) {
        err = "cannot rename to " + target;
        remove(tmp.c_str());
        return false;
    }
    if (have) {
        struct timespec times[2];
        times[0] = st.st_atim;
        times[1] = st.st_mtim;
        utimensat(AT_FDCWD, target.c_str(), times, 0);
    }
    return true;
}

bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

uint64_t file_size(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return static_cast<uint64_t>(st.st_size);
}

std::string self_path() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    return std::string(buf);
}

std::string format_bytes(uint64_t bytes) {
    char buf[64];
    if (bytes >= 1024ULL * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return buf;
}

std::string format_pct(double ratio) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f%%", (1.0 - ratio) * 100.0);
    return buf;
}

bool make_temp_dir(const std::string& base, std::string& out_dir, std::string& err) {
    std::filesystem::path parent = base.empty() ? std::filesystem::temp_directory_path()
                                                : std::filesystem::path(base);
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        err = "cannot create temp dir base " + parent.string();
        return false;
    }
    std::random_device rd;
    std::mt19937_64 gen(rd());
    for (int i = 0; i < 100; ++i) {
        std::filesystem::path d = parent / ("jpegopt-" + std::to_string(gen()));
        if (std::filesystem::create_directory(d, ec) && !ec) {
            out_dir = d.string();
            return true;
        }
        ec.clear();
    }
    err = "cannot create unique temp dir in " + parent.string();
    return false;
}

bool remove_all(const std::string& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return !ec;
}

std::string output_path_for(const std::string& input_path) {
    std::filesystem::path p(input_path);
    std::string ext = p.extension().string();
    std::string stem = p.stem().string();
    return (p.parent_path() / (stem + ".opt" + ext)).string();
}

}  // namespace util
