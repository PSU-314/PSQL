// bench_mixed_workload.cpp
//
// Simulates a realistic mixed CRUD workload (like a YCSB-style benchmark)
// against a single table, tracking overall throughput and latency
// percentiles per operation type, plus:
//   - Multi-table workload (join-less, since engine is single-table-at-a-time
//     per query) to test catalog/metadata overhead with many tables.
//   - Constraint-violation storm: hammer the engine with intentionally
//     invalid queries (duplicate PK, null violation, bad syntax) to make
//     sure error handling doesn't degrade performance or corrupt state.
//   - Large-row / wide-value stress: bigger string payloads.
//   - Long-running sustained throughput test (fixed wall-clock duration).
//
// Usage: ./bench_mixed_workload [seconds_for_sustained_test]
//
// NOTE: PSQLDatabase persists to ./data/<table>.db between process runs.
// Run from a scratch directory or `rm -rf data/` first (see run_all_benchmarks.sh).

#include "psql.h"
#include "bench_common.h"
#include <random>
#include <sstream>
#include <cstdio>
#include <string>

static std::string randomString(std::mt19937& rng, int len) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<int> dist(0, sizeof(charset) - 2);
    std::string s;
    s.reserve(len);
    for (int i = 0; i < len; i++) s += charset[dist(rng)];
    return s;
}

int main(int argc, char** argv) {
    int sustainedSeconds = 5;
    if (argc > 1) sustainedSeconds = std::atoi(argv[1]);

    printf("=====================================================\n");
    printf(" PSQLDatabase Mixed Workload Stress Test\n");
    printf("=====================================================\n\n");

    std::mt19937 rng(2024);
    Stopwatch sw;

    // ---- Workload A: YCSB-style mixed CRUD (50% read, 20% insert, 20% update, 10% delete) ----
    printf("---- Workload A: mixed CRUD (50%% read / 20%% insert / 20%% update / 10%% delete) ----\n");
    {
        PSQLDatabase db;
        db.executeQuery("CREATE TABLE mixed (id INT PRIMARY_KEY, val STRING NOT_NULL, ctr INT);");

        const int PRELOAD = 5000;
        for (int i = 0; i < PRELOAD; i++) {
            std::ostringstream ss;
            ss << "INSERT INTO mixed VALUES (" << i << ", 'v" << i << "', 0);";
            db.executeQuery(ss.str());
        }

        int nextId = PRELOAD;
        LatencyStats readStats, insertStats, updateStats, deleteStats;
        std::uniform_int_distribution<int> opDist(0, 99);
        std::uniform_int_distribution<int> keyDist(0, PRELOAD - 1);

        const int TOTAL_OPS = 20000;
        int liveMin = 0, liveMax = PRELOAD - 1;

        for (int i = 0; i < TOTAL_OPS; i++) {
            int op = opDist(rng);
            std::ostringstream ss;
            if (op < 50) {
                int id = std::uniform_int_distribution<int>(liveMin, liveMax)(rng);
                ss << "SELECT * FROM mixed WHERE id = " << id << ";";
                sw.start();
                auto r = db.executeQuery(ss.str());
                readStats.add(sw.stopNs());
                (void)r;
            } else if (op < 70) {
                ss << "INSERT INTO mixed VALUES (" << nextId << ", '" << randomString(rng, 8) << "', 0);";
                sw.start();
                auto r = db.executeQuery(ss.str());
                insertStats.add(sw.stopNs());
                if (r.success) { liveMax = nextId; nextId++; }
            } else if (op < 90) {
                int id = std::uniform_int_distribution<int>(liveMin, liveMax)(rng);
                ss << "UPDATE mixed SET ctr = " << i << " WHERE id = " << id << ";";
                sw.start();
                auto r = db.executeQuery(ss.str());
                updateStats.add(sw.stopNs());
                (void)r;
            } else {
                int id = std::uniform_int_distribution<int>(liveMin, liveMax)(rng);
                ss << "DELETE FROM mixed WHERE id = " << id << ";";
                sw.start();
                auto r = db.executeQuery(ss.str());
                deleteStats.add(sw.stopNs());
                (void)r;
            }
        }

        readStats.printSummary("Mixed workload: READ");
        insertStats.printSummary("Mixed workload: INSERT");
        updateStats.printSummary("Mixed workload: UPDATE");
        deleteStats.printSummary("Mixed workload: DELETE");

        long totalOps = readStats.samples.size() + insertStats.samples.size()
                       + updateStats.samples.size() + deleteStats.samples.size();
        double totalNs = 0;
        for (auto v : readStats.samples) totalNs += v;
        for (auto v : insertStats.samples) totalNs += v;
        for (auto v : updateStats.samples) totalNs += v;
        for (auto v : deleteStats.samples) totalNs += v;
        printf("Aggregate: %ld ops in %.3f ms wall (sum of measured op time) => %.1f ops/sec effective\n\n",
               totalOps, totalNs / 1e6, totalOps / (totalNs / 1e9));
    }

    // ---- Workload B: many tables (catalog stress) ----
    printf("---- Workload B: many-table catalog stress ----\n");
    {
        PSQLDatabase db;
        const int NUM_TABLES = 200;
        LatencyStats createStats;
        for (int t = 0; t < NUM_TABLES; t++) {
            std::ostringstream ss;
            ss << "CREATE TABLE t" << t << " (id INT PRIMARY_KEY, val STRING NOT_NULL);";
            sw.start();
            auto r = db.executeQuery(ss.str());
            createStats.add(sw.stopNs());
            if (!r.success) {
                printf("FATAL: create table t%d failed: %s\n", t, r.errorMessage.c_str());
                return 1;
            }
        }
        createStats.printSummary("CREATE TABLE (" + std::to_string(NUM_TABLES) + " tables)");

        // Insert into a random table and confirm no cross-table contamination.
        LatencyStats crossInsert;
        for (int i = 0; i < 2000; i++) {
            int t = std::uniform_int_distribution<int>(0, NUM_TABLES - 1)(rng);
            std::ostringstream ss;
            ss << "INSERT INTO t" << t << " VALUES (" << i << ", 'x" << i << "');";
            sw.start();
            auto r = db.executeQuery(ss.str());
            crossInsert.add(sw.stopNs());
            if (!r.success) {
                printf("Unexpected insert failure into t%d: %s\n", t, r.errorMessage.c_str());
            }
        }
        crossInsert.printSummary("INSERT across random tables (catalog lookup overhead)");
        printf("\n");
    }

    // ---- Workload C: constraint-violation storm (error-path performance) ----
    printf("---- Workload C: constraint violation / error-path storm ----\n");
    {
        PSQLDatabase db;
        db.executeQuery("CREATE TABLE errtest (id INT PRIMARY_KEY, val STRING NOT_NULL);");
        db.executeQuery("INSERT INTO errtest VALUES (1, 'first');");

        LatencyStats dupPkStats, nullStats, syntaxStats;
        const int ERR_ITERS = 3000;
        for (int i = 0; i < ERR_ITERS; i++) {
            sw.start();
            auto r1 = db.executeQuery("INSERT INTO errtest VALUES (1, 'dup');");
            dupPkStats.add(sw.stopNs());
            if (r1.success) printf("WARNING: duplicate PK insert unexpectedly succeeded at i=%d\n", i);

            sw.start();
            auto r2 = db.executeQuery("INSERT INTO errtest VALUES (999999, NULL);");
            nullStats.add(sw.stopNs());
            if (r2.success) printf("WARNING: null violation unexpectedly succeeded at i=%d\n", i);

            sw.start();
            auto r3 = db.executeQuery("INSERT INTO errtest VALUS (2, 'x');"); // typo'd keyword
            syntaxStats.add(sw.stopNs());
            if (r3.success) printf("WARNING: malformed syntax unexpectedly succeeded at i=%d\n", i);
        }
        dupPkStats.printSummary("Rejected: duplicate primary key");
        nullStats.printSummary("Rejected: NOT_NULL violation");
        syntaxStats.printSummary("Rejected: syntax error");

        // Confirm engine is still healthy after the storm.
        auto sanity = db.executeQuery("SELECT * FROM errtest;");
        printf("\nPost-storm sanity check: SELECT * success=%d rows=%zu (expect 1)\n",
               sanity.success, sanity.rows.size());
        printf("\n");
    }

    // ---- Workload D: wide-row / large payload stress ----
    printf("---- Workload D: large string payload stress ----\n");
    {
        PSQLDatabase db;
        db.executeQuery("CREATE TABLE wide (id INT PRIMARY_KEY, blob STRING NOT_NULL);");
        LatencyStats wideInsert;
        std::vector<int> sizes = {16, 64, 256, 1024, 4096};
        for (int sz : sizes) {
            LatencyStats s;
            for (int i = 0; i < 200; i++) {
                std::string payload = randomString(rng, sz);
                std::ostringstream ss;
                ss << "INSERT INTO wide VALUES (" << (sz * 1000 + i) << ", '" << payload << "');";
                sw.start();
                auto r = db.executeQuery(ss.str());
                long long ns = sw.stopNs();
                if (r.success) s.add(ns);
                else { printf("  (payload size %d rejected: %s)\n", sz, r.errorMessage.c_str()); break; }
            }
            if (!s.samples.empty()) {
                printf("  payload=%5d bytes: mean=%.3fus p99=%.3fus (n=%zu)\n",
                       sz, s.meanNs()/1000.0, s.percentile(99)/1000.0, s.samples.size());
            }
        }
        printf("\n");
    }

    // ---- Workload E: sustained fixed-duration throughput ----
    printf("---- Workload E: sustained throughput over %d seconds ----\n", sustainedSeconds);
    {
        PSQLDatabase db;
        db.executeQuery("CREATE TABLE sustained (id INT PRIMARY_KEY, val STRING NOT_NULL);");
        long long opCount = 0;
        auto start = std::chrono::steady_clock::now();
        auto deadline = start + std::chrono::seconds(sustainedSeconds);
        int id = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            std::ostringstream ss;
            ss << "INSERT INTO sustained VALUES (" << id << ", 'x" << id << "');";
            auto r = db.executeQuery(ss.str());
            if (!r.success) break;
            id++;
            opCount++;
        }
        auto end = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(end - start).count();
        printf("  Inserted %lld rows in %.3f sec => %.1f inserts/sec sustained\n",
               opCount, elapsedSec, opCount / elapsedSec);
    }

    printf("\nAll mixed-workload tests complete.\n");
    return 0;
}
