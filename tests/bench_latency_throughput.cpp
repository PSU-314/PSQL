// bench_latency_throughput.cpp
//
// Measures per-operation latency (min/mean/stddev/p50/p90/p99/p99.9/max) and
// derived throughput (ops/sec) for the core CRUD operations of PSQLDatabase:
//   - INSERT (sequential keys)
//   - INSERT (random keys)
//   - SELECT * (full table scan) at increasing table sizes
//   - SELECT point lookup (WHERE id = X) at increasing table sizes
//   - UPDATE (point)
//   - DELETE (point)
//
// Usage: ./bench_latency_throughput [N]
//   N = number of rows to use for the base dataset (default 20000)
//
// IMPORTANT: PSQLDatabase persists tables to disk (./data/<table>.db plus a
// shared ./data/catalog.meta), keyed by table name, and re-running a binary
// without clearing that directory will hit "Table already exists" errors.
// Run this from a scratch working directory, or `rm -rf data/` between runs
// (see run_all_benchmarks.sh, which does this automatically).

#include "psql.h"
#include "bench_common.h"
#include <random>
#include <cstdio>
#include <sstream>

static std::string q_create() {
    return "CREATE TABLE bench (id INT PRIMARY_KEY, val STRING NOT_NULL, score INT);";
}

static std::string q_insert(int id, const std::string& val, int score) {
    std::ostringstream ss;
    ss << "INSERT INTO bench VALUES (" << id << ", '" << val << "', " << score << ");";
    return ss.str();
}

static std::string q_select_point(int id) {
    std::ostringstream ss;
    ss << "SELECT * FROM bench WHERE id = " << id << ";";
    return ss.str();
}

static std::string q_update_point(int id, int newScore) {
    std::ostringstream ss;
    ss << "UPDATE bench SET score = " << newScore << " WHERE id = " << id << ";";
    return ss.str();
}

static std::string q_delete_point(int id) {
    std::ostringstream ss;
    ss << "DELETE FROM bench WHERE id = " << id << ";";
    return ss.str();
}

int main(int argc, char** argv) {
    int N = 20000;
    if (argc > 1) N = std::atoi(argv[1]);

    printf("=====================================================\n");
    printf(" PSQLDatabase Latency & Throughput Benchmark\n");
    printf(" Base dataset size N = %d\n", N);
    printf("=====================================================\n\n");

    PSQLDatabase db;

    auto r = db.executeQuery(q_create());
    if (!r.success) {
        printf("FATAL: failed to create table: %s\n", r.errorMessage.c_str());
        return 1;
    }

    std::mt19937 rng(12345);

    // ---- 1. Sequential INSERT latency ----
    LatencyStats insertSeq;
    Stopwatch sw;
    for (int i = 0; i < N; i++) {
        std::string query = q_insert(i, "value_" + std::to_string(i), i % 1000);
        sw.start();
        auto res = db.executeQuery(query);
        long long ns = sw.stopNs();
        if (!res.success) {
            printf("Unexpected insert failure at i=%d: %s\n", i, res.errorMessage.c_str());
            return 1;
        }
        insertSeq.add(ns);
    }
    insertSeq.printSummary("Sequential INSERT (" + std::to_string(N) + " rows)");
    printf("\n");

    // ---- 2. Full table scan latency at current size N ----
    LatencyStats scanStats;
    for (int trial = 0; trial < 50; trial++) {
        sw.start();
        auto res = db.executeQuery("SELECT * FROM bench;");
        long long ns = sw.stopNs();
        if (!res.success || (int)res.rows.size() != N) {
            printf("Unexpected scan result: success=%d rows=%zu (expected %d)\n",
                   res.success, res.rows.size(), N);
        }
        scanStats.add(ns);
    }
    scanStats.printSummary("Full table SCAN (SELECT *, " + std::to_string(N) + " rows)");
    printf("\n");

    // ---- 3. Point SELECT latency (random existing keys) ----
    LatencyStats pointSelect;
    std::uniform_int_distribution<int> distExisting(0, N - 1);
    const int POINT_TRIALS = 5000;
    for (int t = 0; t < POINT_TRIALS; t++) {
        int id = distExisting(rng);
        sw.start();
        auto res = db.executeQuery(q_select_point(id));
        long long ns = sw.stopNs();
        if (!res.success || res.rows.size() != 1) {
            printf("Unexpected point select failure for id=%d\n", id);
        }
        pointSelect.add(ns);
    }
    pointSelect.printSummary("Point SELECT (WHERE id = X), " + std::to_string(POINT_TRIALS) + " trials");
    printf("\n");

    // ---- 4. Point UPDATE latency ----
    LatencyStats updateStats;
    for (int t = 0; t < POINT_TRIALS; t++) {
        int id = distExisting(rng);
        sw.start();
        auto res = db.executeQuery(q_update_point(id, t % 1000));
        long long ns = sw.stopNs();
        if (!res.success) {
            printf("Unexpected update failure for id=%d: %s\n", id, res.errorMessage.c_str());
        }
        updateStats.add(ns);
    }
    updateStats.printSummary("Point UPDATE (WHERE id = X), " + std::to_string(POINT_TRIALS) + " trials");
    printf("\n");

    // ---- 5. Point DELETE + reinsert latency (keeps table size stable at N) ----
    LatencyStats deleteStats, reinsertStats;
    const int DEL_TRIALS = 2000;
    std::vector<int> deletedIds;
    std::uniform_int_distribution<int> distFull(0, N - 1);
    for (int t = 0; t < DEL_TRIALS; t++) {
        int id = distFull(rng);
        sw.start();
        auto res = db.executeQuery(q_delete_point(id));
        long long ns = sw.stopNs();
        if (res.success && res.rowsAffected > 0) {
            deletedIds.push_back(id);
            deleteStats.add(ns);
        }
    }
    for (int id : deletedIds) {
        sw.start();
        auto res = db.executeQuery(q_insert(id, "restored_" + std::to_string(id), id % 1000));
        long long ns = sw.stopNs();
        if (res.success) reinsertStats.add(ns);
    }
    deleteStats.printSummary("Point DELETE (WHERE id = X), " + std::to_string(deleteStats.samples.size()) + " actual deletes");
    printf("\n");
    reinsertStats.printSummary("Reinsert after delete, " + std::to_string(reinsertStats.samples.size()) + " rows");
    printf("\n");

    // ---- 6. Random-order (non-sequential key) INSERT into a fresh table ----
    // Tests whether B-tree rebalancing cost differs for random vs sequential keys.
    {
        PSQLDatabase db2;
        auto rc = db2.executeQuery("CREATE TABLE bench_rand (id INT PRIMARY_KEY, val STRING NOT_NULL, score INT);");
        if (!rc.success) {
            printf("FATAL: could not create bench_rand table: %s\n", rc.errorMessage.c_str());
            printf("(If this table already exists from a prior run, delete ./data/ and retry.)\n");
            return 1;
        }
        std::vector<int> ids(N);
        for (int i = 0; i < N; i++) ids[i] = i;
        std::shuffle(ids.begin(), ids.end(), rng);

        LatencyStats insertRand;
        for (int i = 0; i < N; i++) {
            std::ostringstream ss2;
            ss2 << "INSERT INTO bench_rand VALUES (" << ids[i] << ", 'value_" << ids[i] << "', " << (ids[i] % 1000) << ");";
            sw.start();
            auto res = db2.executeQuery(ss2.str());
            long long ns = sw.stopNs();
            if (!res.success) {
                printf("Unexpected random insert failure at id=%d: %s\n", ids[i], res.errorMessage.c_str());
                return 1;
            }
            insertRand.add(ns);
        }
        insertRand.printSummary("Random-order INSERT (" + std::to_string(N) + " rows, shuffled keys)");
        printf("\n");

        // CSV export for this section too
        printf("CSV,label,count,min_us,mean_us,p50_us,p90_us,p99_us,p999_us,max_us,throughput_ops_sec\n");
        insertRand.printCsvRow("insert_random_order");
    }

    // ---- CSV summary block (all sections) ----
    printf("\nCSV,label,count,min_us,mean_us,p50_us,p90_us,p99_us,p999_us,max_us,throughput_ops_sec\n");
    insertSeq.printCsvRow("insert_sequential");
    scanStats.printCsvRow("full_scan_select_star");
    pointSelect.printCsvRow("point_select_where_id");
    updateStats.printCsvRow("point_update_where_id");
    deleteStats.printCsvRow("point_delete_where_id");
    reinsertStats.printCsvRow("reinsert_after_delete");

    printf("\nDone.\n");
    return 0;
}
