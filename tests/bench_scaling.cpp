// bench_scaling.cpp
//
// Measures how operation latency scales as the table grows. For a proper
// B-tree, INSERT/SELECT/UPDATE/DELETE should scale ~O(log n). This test
// inserts data in increasing checkpoints and measures the average latency
// of a batch of operations performed *at each checkpoint size*, so you can
// plot latency vs. table size and see whether it's flat, logarithmic, or
// (red flag) linear/quadratic.
//
// Usage: ./bench_scaling [max_n] [num_checkpoints]
//   max_n            = final table size (default 100000)
//   num_checkpoints  = how many measurement points between 0 and max_n (default 10)
//
// NOTE: PSQLDatabase persists to ./data/<table>.db between process runs.
// Run from a scratch directory or `rm -rf data/` first (see run_all_benchmarks.sh).

#include "psql.h"
#include "bench_common.h"
#include <random>
#include <sstream>
#include <cstdio>

static std::string q_insert(int id) {
    std::ostringstream ss;
    ss << "INSERT INTO scaling VALUES (" << id << ", 'row_" << id << "', " << (id % 500) << ");";
    return ss.str();
}
static std::string q_select_point(int id) {
    std::ostringstream ss;
    ss << "SELECT * FROM scaling WHERE id = " << id << ";";
    return ss.str();
}
static std::string q_update_point(int id) {
    std::ostringstream ss;
    ss << "UPDATE scaling SET score = " << (id % 777) << " WHERE id = " << id << ";";
    return ss.str();
}

int main(int argc, char** argv) {
    int maxN = 100000;
    int checkpoints = 10;
    if (argc > 1) maxN = std::atoi(argv[1]);
    if (argc > 2) checkpoints = std::atoi(argv[2]);

    printf("=====================================================\n");
    printf(" PSQLDatabase Scaling Benchmark (latency vs table size)\n");
    printf(" max_n=%d checkpoints=%d\n", maxN, checkpoints);
    printf("=====================================================\n\n");

    PSQLDatabase db;
    auto r = db.executeQuery("CREATE TABLE scaling (id INT PRIMARY_KEY, tag STRING NOT_NULL, score INT);");
    if (!r.success) {
        printf("FATAL: create table failed: %s\n", r.errorMessage.c_str());
        return 1;
    }

    std::mt19937 rng(999);
    Stopwatch sw;

    int step = maxN / checkpoints;
    int inserted = 0;

    printf("CSV,table_size,op,mean_us,p50_us,p99_us,max_us\n");

    for (int cp = 1; cp <= checkpoints; cp++) {
        int target = step * cp;

        // Grow the table up to `target` rows, measuring insert latency for this growth segment.
        LatencyStats growInsert;
        while (inserted < target) {
            sw.start();
            auto res = db.executeQuery(q_insert(inserted));
            long long ns = sw.stopNs();
            if (!res.success) {
                printf("FATAL insert failure at id=%d: %s\n", inserted, res.errorMessage.c_str());
                return 1;
            }
            growInsert.add(ns);
            inserted++;
        }

        // Measure point SELECT latency at this table size.
        LatencyStats selStats;
        std::uniform_int_distribution<int> dist(0, inserted - 1);
        for (int t = 0; t < 300; t++) {
            int id = dist(rng);
            sw.start();
            auto res = db.executeQuery(q_select_point(id));
            long long ns = sw.stopNs();
            if (!res.success || res.rows.size() != 1) {
                printf("Unexpected select failure at id=%d (table_size=%d)\n", id, inserted);
            }
            selStats.add(ns);
        }

        // Measure point UPDATE latency at this table size.
        LatencyStats updStats;
        for (int t = 0; t < 300; t++) {
            int id = dist(rng);
            sw.start();
            auto res = db.executeQuery(q_update_point(id));
            long long ns = sw.stopNs();
            if (!res.success) {
                printf("Unexpected update failure at id=%d (table_size=%d)\n", id, inserted);
            }
            updStats.add(ns);
        }

        printf("---- Table size: %d rows ----\n", inserted);
        printf("  INSERT (this segment, avg of %zu ops): mean=%.3fus p50=%.3fus p99=%.3fus max=%.3fus\n",
               growInsert.samples.size(), growInsert.meanNs()/1000.0, growInsert.percentile(50)/1000.0,
               growInsert.percentile(99)/1000.0, growInsert.maxNs()/1000.0);
        printf("  SELECT (point, 300 ops):              mean=%.3fus p50=%.3fus p99=%.3fus max=%.3fus\n",
               selStats.meanNs()/1000.0, selStats.percentile(50)/1000.0,
               selStats.percentile(99)/1000.0, selStats.maxNs()/1000.0);
        printf("  UPDATE (point, 300 ops):               mean=%.3fus p50=%.3fus p99=%.3fus max=%.3fus\n",
               updStats.meanNs()/1000.0, updStats.percentile(50)/1000.0,
               updStats.percentile(99)/1000.0, updStats.maxNs()/1000.0);
        printf("\n");

        printf("CSV,%d,insert,%.3f,%.3f,%.3f,%.3f\n", inserted, growInsert.meanNs()/1000.0,
               growInsert.percentile(50)/1000.0, growInsert.percentile(99)/1000.0, growInsert.maxNs()/1000.0);
        printf("CSV,%d,select,%.3f,%.3f,%.3f,%.3f\n", inserted, selStats.meanNs()/1000.0,
               selStats.percentile(50)/1000.0, selStats.percentile(99)/1000.0, selStats.maxNs()/1000.0);
        printf("CSV,%d,update,%.3f,%.3f,%.3f,%.3f\n", inserted, updStats.meanNs()/1000.0,
               updStats.percentile(50)/1000.0, updStats.percentile(99)/1000.0, updStats.maxNs()/1000.0);
    }

    // Simple heuristic check: compare mean insert latency of the first checkpoint segment
    // vs the last. Flags if growth looks worse than logarithmic.
    printf("\nInterpretation hint: if 'INSERT (this segment)' mean latency at the largest\n");
    printf("table size is more than ~3-5x the latency at the smallest table size, growth is\n");
    printf("likely worse than O(log n) (could be O(sqrt n) or O(n) due to node splits/rebalancing\n");
    printf("or a linear scan happening somewhere in the insert path).\n");

    return 0;
}
