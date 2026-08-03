// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "cli.h"
#include "pipeline.h"
#include "report.h"

int main(int argc, char** argv) {
    CliOptions opts;
    std::string err;
    if (!parse_cli(argc, argv, opts, err)) {
        fprintf(stderr, "jpegopt: %s\n\n%s", err.c_str(), usage_text().c_str());
        return 2;
    }
    if (opts.help) {
        fputs(usage_text().c_str(), stdout);
        return 0;
    }
    if (opts.version) {
        printf("jpegopt %s\n", JPEGOPT_VERSION);
        return 0;
    }
    if (opts.inputs.empty()) {
        fprintf(stderr, "jpegopt: no input files\n\n%s", usage_text().c_str());
        return 2;
    }

    std::vector<std::string> files;
    collect_inputs(opts.inputs, files);
    if (files.empty()) {
        fprintf(stderr, "jpegopt: no JPEG files found\n");
        return 2;
    }

    int threads = opts.threads;
    if (threads <= 0) {
        unsigned int cores = std::thread::hardware_concurrency();
        if (cores == 0) cores = 2;
        threads = static_cast<int>(cores / 4);
        if (threads < 1) threads = 1;
        if (threads > 4) threads = 4;
    }

    PipelineOptions popts;
    popts.allow_arith = opts.allow_arith;
    popts.dry_run = opts.dry_run;
    popts.in_place = opts.in_place;
    popts.verbose = opts.verbose;
    popts.strip_metadata = opts.strip_metadata;
    popts.temp_dir = opts.temp_dir;

    std::vector<FileResult> results(files.size());
    std::atomic<size_t> next{0};
    auto worker = [&]() {
        for (;;) {
            size_t idx = next.fetch_add(1);
            if (idx >= files.size()) break;
            results[idx] = process_file(files[idx], popts);
        }
    };

    std::vector<std::thread> pool;
    for (int i = 0; i < threads; ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    std::string report;
    build_report(results, opts.json, opts.show_all, report);
    fputs(report.c_str(), stdout);

    bool any_error = false;
    for (const auto& r : results) {
        if (!r.ok) any_error = true;
    }
    return any_error ? 1 : 0;
}
