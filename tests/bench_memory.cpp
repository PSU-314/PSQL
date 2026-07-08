// bench_memory.cpp
//
// Memory-behavior test (independent of Valgrind, meant to run fast and show
// a trend). Strategy:
//   1. Repeatedly construct a PSQLDatabase, insert M rows, delete them all,
//      destroy the DB object, and record RSS after each full cycle.
//      If RSS keeps climbing cycle over cycle, that's a strong leak signal
//      (either in the engine's page/buffer pool or in result-set handling).
//   2. Within a single long-lived DB, run many SELECT queries that allocate
//      ResultSet objects (vectors of strings) and confirm RSS returns to
//      baseline after those ResultSets go out of scope.
//   3. Report a churn test: repeated insert+delete of the same key many
//      times, to catch leaks specific to the delete path (e.g. leaked
//      B-tree nodes/pages after a leaf merge).
//
// This is a heuristic smoke test
// Usage: ./bench_memory [cycles] [rows_per_cycle]
//
// NOTE: PSQLDatabase persists to ./data/<table>.db between process runs.
// Run from a scratch directory or `rm -rf data/` first (see run_all_benchmarks.sh).

#include "psql.h"
#include "bench_common.h"
#include <sstream>
#include <cstdio>
#include <vector>

static std::string q_insert(int id) {
    std::ostringstream ss;
    ss << "INSERT INTO memtest VALUES (" << id << ", 'payload_" << id << "');";
    return ss.str();
}
static std::string q_delete(int id) {
    std::ostringstream ss;
    ss << "DELETE FROM memtest WHERE id = " << id << ";";
    return ss.str();
}

int main(int argc, char** argv) {
    int cycles = 20;
    int rowsPerCycle = 5000;
    if (argc > 1) cycles = std::atoi(argv[1]);
    if (argc > 2) rowsPerCycle = std::atoi(argv[2]);

    printf("=====================================================\n");
    printf(" PSQLDatabase Memory Behavior Test\n");
    printf(" cycles=%d rows_per_cycle=%d\n", cycles, rowsPerCycle);
    printf("=====================================================\n\n");

    // ---- Part 1: full create/populate/drain/destroy cycles ----
    printf("---- Part 1: DB lifecycle cycles (construct -> fill -> empty -> destruct) ----\n");
    printf("CSV,cycle,rss_kb_after_cycle\n");
    std::vector<long> rssAfterCycle;
    for (int c = 0; c < cycles; c++) {
        {
            PSQLDatabase db;
            auto r = db.executeQuery("CREATE TABLE memtest (id INT PRIMARY_KEY, data STRING NOT_NULL);");
            if (!r.success) {
                printf("FATAL: create failed on cycle %d: %s\n", c, r.errorMessage.c_str());
                return 1;
            }
            for (int i = 0; i < rowsPerCycle; i++) {
                auto res = db.executeQuery(q_insert(i));
                if (!res.success) {
                    printf("FATAL: insert failed cycle=%d i=%d: %s\n", c, i, res.errorMessage.c_str());
                    return 1;
                }
            }
            for (int i = 0; i < rowsPerCycle; i++) {
                db.executeQuery(q_delete(i));
            }
            // PSQLDatabase persists tables to disk keyed by name (./data/memtest.db),
            // so we must DROP the table before this scope ends, or the next cycle's
            // CREATE TABLE will fail with "already exists".
            auto dr = db.executeQuery("DROP TABLE memtest;");
            if (!dr.success) {
                printf("FATAL: DROP TABLE failed on cycle %d: %s\n", c, dr.errorMessage.c_str());
                return 1;
            }
            // db destructs here (end of scope)
        }
        long rss = getCurrentRSSKb();
        rssAfterCycle.push_back(rss);
        printf("  cycle %3d/%d complete. RSS after destruct: %ld KB\n", c + 1, cycles, rss);
        printf("CSV,%d,%ld\n", c + 1, rss);
    }

    // Trend check: compare average RSS of the first quarter of cycles to the last quarter.
    {
        int q = std::max(1, cycles / 4);
        long firstSum = 0, lastSum = 0;
        for (int i = 0; i < q; i++) firstSum += rssAfterCycle[i];
        for (int i = cycles - q; i < cycles; i++) lastSum += rssAfterCycle[i];
        double firstAvg = (double)firstSum / q;
        double lastAvg = (double)lastSum / q;
        double growthPct = firstAvg > 0 ? (lastAvg - firstAvg) / firstAvg * 100.0 : 0.0;
        printf("\n  First %d cycles avg RSS: %.1f KB\n", q, firstAvg);
        printf("  Last  %d cycles avg RSS: %.1f KB\n", q, lastAvg);
        printf("  Growth: %.1f%%\n", growthPct);
        if (growthPct > 15.0) {
            printf("  ** WARNING: RSS grew >15%% across repeated full lifecycle runs.\n");
            printf("     This suggests memory is not fully released across DB construct/destruct.\n");
        } else {
            printf("  OK: RSS stayed roughly stable across repeated lifecycle runs.\n");
        }
    }

    // ---- Part 2: ResultSet churn within one long-lived DB ----
    printf("\n---- Part 2: ResultSet allocation churn (SELECT * repeated) ----\n");
    {
        PSQLDatabase db;
        auto cr = db.executeQuery("CREATE TABLE churn (id INT PRIMARY_KEY, data STRING NOT_NULL);");
        if (!cr.success) {
            printf("FATAL: create 'churn' failed: %s (delete ./data/ and retry)\n", cr.errorMessage.c_str());
            return 1;
        }
        for (int i = 0; i < 2000; i++) db.executeQuery(q_insert(i));

        long rssBefore = getCurrentRSSKb();
        printf("  RSS before churn: %ld KB\n", rssBefore);
        const int SELECT_ITERS = 3000;
        for (int i = 0; i < SELECT_ITERS; i++) {
            auto r = db.executeQuery("SELECT * FROM churn;"); // ResultSet destructs each loop iter
            (void)r;
        }
        long rssAfter = getCurrentRSSKb();
        printf("  RSS after %d SELECT * calls (2000-row table): %ld KB\n", SELECT_ITERS, rssAfter);
        double growthPct = rssBefore > 0 ? (double)(rssAfter - rssBefore) / rssBefore * 100.0 : 0.0;
        printf("  Growth: %.1f%%\n", growthPct);
        if (growthPct > 10.0) {
            printf("  ** WARNING: RSS grew during repeated SELECT churn -- possible leak in\n");
            printf("     ResultSet/Row construction or query execution path.\n");
        } else {
            printf("  OK: RSS stable during ResultSet churn.\n");
        }
    }

    // ---- Part 3: repeated insert+delete of the SAME key (delete-path / merge stress) ----
    printf("\n---- Part 3: same-key insert/delete churn (B-tree merge/rebalance stress) ----\n");
    {
        PSQLDatabase db;
        auto kr = db.executeQuery("CREATE TABLE keychurn (id INT PRIMARY_KEY, data STRING NOT_NULL);");
        if (!kr.success) {
            printf("FATAL: create 'keychurn' failed: %s (delete ./data/ and retry)\n", kr.errorMessage.c_str());
            return 1;
        }
        long rssBefore = getCurrentRSSKb();
        const int KEY_CHURN_ITERS = 10000;
        for (int i = 0; i < KEY_CHURN_ITERS; i++) {
            db.executeQuery("INSERT INTO keychurn VALUES (1, 'x');");
            db.executeQuery("DELETE FROM keychurn WHERE id = 1;");
        }
        long rssAfter = getCurrentRSSKb();
        printf("  RSS before: %ld KB, after %d insert/delete pairs on same key: %ld KB\n",
               rssBefore, KEY_CHURN_ITERS, rssAfter);
        double growthPct = rssBefore > 0 ? (double)(rssAfter - rssBefore) / rssBefore * 100.0 : 0.0;
        printf("  Growth: %.1f%%\n", growthPct);
        if (growthPct > 10.0) {
            printf("  ** WARNING: possible leak in delete/rebalance path (pager not reclaiming pages).\n");
        } else {
            printf("  OK: stable under same-key churn.\n");
        }
    }

    printf("\nDone. Pair this with bench_memory_valgrind.sh for a definitive leak report.\n");
    return 0;
}
