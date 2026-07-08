#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <chrono>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <numeric>
#include <cmath>
#include <cstring>

// Simple high-resolution stopwatch returning nanoseconds.
class Stopwatch {
public:
    void start() { t0 = std::chrono::steady_clock::now(); }
    // Returns elapsed nanoseconds since start().
    long long stopNs() {
        auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    }
private:
    std::chrono::steady_clock::time_point t0;
};

// Holds a batch of per-operation latencies (nanoseconds) and derives stats.
struct LatencyStats {
    std::vector<long long> samples; // ns

    void add(long long ns) { samples.push_back(ns); }

    double meanNs() const {
        if (samples.empty()) return 0.0;
        long long sum = std::accumulate(samples.begin(), samples.end(), 0LL);
        return (double)sum / samples.size();
    }

    double stddevNs() const {
        if (samples.size() < 2) return 0.0;
        double m = meanNs();
        double acc = 0.0;
        for (auto s : samples) { double d = (double)s - m; acc += d * d; }
        return std::sqrt(acc / (samples.size() - 1));
    }

    // percentile in [0,100]
    long long percentile(double p) const {
        if (samples.empty()) return 0;
        std::vector<long long> sorted(samples);
        std::sort(sorted.begin(), sorted.end());
        double idx = (p / 100.0) * (sorted.size() - 1);
        size_t lo = (size_t)std::floor(idx);
        size_t hi = (size_t)std::ceil(idx);
        if (lo == hi) return sorted[lo];
        double frac = idx - lo;
        return (long long)(sorted[lo] + frac * (sorted[hi] - sorted[lo]));
    }

    long long minNs() const { return samples.empty() ? 0 : *std::min_element(samples.begin(), samples.end()); }
    long long maxNs() const { return samples.empty() ? 0 : *std::max_element(samples.begin(), samples.end()); }

    void printSummary(const std::string& label) const {
        printf("---- %s ----\n", label.c_str());
        printf("  samples   : %zu\n", samples.size());
        printf("  min       : %.3f us\n", minNs() / 1000.0);
        printf("  mean      : %.3f us\n", meanNs() / 1000.0);
        printf("  stddev    : %.3f us\n", stddevNs() / 1000.0);
        printf("  p50       : %.3f us\n", percentile(50) / 1000.0);
        printf("  p90       : %.3f us\n", percentile(90) / 1000.0);
        printf("  p99       : %.3f us\n", percentile(99) / 1000.0);
        printf("  p99.9     : %.3f us\n", percentile(99.9) / 1000.0);
        printf("  max       : %.3f us\n", maxNs() / 1000.0);
        if (meanNs() > 0)
            printf("  throughput: %.1f ops/sec (from mean latency)\n", 1e9 / meanNs());
    }

    // CSV row: label,count,min_us,mean_us,p50_us,p90_us,p99_us,p999_us,max_us,throughput_ops_sec
    void printCsvRow(const std::string& label) const {
        printf("%s,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f\n",
            label.c_str(), samples.size(),
            minNs()/1000.0, meanNs()/1000.0,
            percentile(50)/1000.0, percentile(90)/1000.0,
            percentile(99)/1000.0, percentile(99.9)/1000.0,
            maxNs()/1000.0, meanNs() > 0 ? 1e9/meanNs() : 0.0);
    }
};

// Reads current process RSS (resident set size) in KB from /proc/self/status.
// Returns -1 if unavailable (e.g. non-Linux).
inline long getCurrentRSSKb() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long rss = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &rss);
            break;
        }
    }
    fclose(f);
    return rss;
}

#endif
