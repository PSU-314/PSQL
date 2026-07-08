# PSQL, A Relational Database Engine Built From Scratch in C++17

**PSQL** is a minimalist, SQLite-inspired relational database management system, engineered from first principles in modern C++. It implements the minimal entirety of a database engine like a hand-rolled SQL compiler, a disk-backed B-Tree storage engine, a buffer pool manager with clock-sweep eviction, and Write-Ahead Logging for crash-safe transactions with **no external database libraries**.

This project was built to understand (and reproduce) the internals of systems like SQLite and Postgres: how a SQL string becomes a validated query plan, how that plan touches disk pages instead of memory blobs, and how a database survives a crash mid-write without corrupting itself.

---

## Architecture

PSQL is organized as a classic multi-layer database pipeline, with each stage independently testable:

```
   SQL string
       │
       ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│    Lexer    │ ──▶│    Parser   │ ──▶│   Executor  │
│ (tokenizer) │     │ (recursive  │     │ (AST → ops) │
│             │     │  descent)   │     │             │
└─────────────┘     └─────────────┘     └───────┬─────┘
                                                │
                                                ▼
                                       ┌──────────────────┐
                                       │  Catalog Manager │
                                       │ (schema persist) │
                                       └──────────────────┘
                                                │
                                                ▼
                                       ┌─────────────────┐
                                       │     B-Tree      │
                                       │ (indexed rows)  │
                                       └────────┬────────┘
                                                │
                                                ▼
                                       ┌──────────────────┐
                                       │  Pager / Buffer  │
                                       │  Pool + WAL      │
                                       └────────┬─────────┘
                                                │
                                                ▼
                                          Disk (.db file)
```

---

## Features

### 1. Hand-Rolled SQL Compiler
- Custom **lexer** tokenizes raw SQL text into keywords, identifiers, literals, and symbols.
- Custom **recursive-descent parser** builds a typed AST (`createStatement`, `insertStatement`, `selectStatement`, `updateStatement`, `deleteStatement`, `dropStatement`) directly from tokens (no parser-generator, no third-party grammar library).
- Enforces schema constraints (`PRIMARY_KEY`, `NOT_NULL`) and type checking (`INT`, `FLOAT`, `CHAR`, `STRING`) at parse/execute time.

### 2. B-Tree Storage Engine
- Rows are stored and indexed in a **disk-backed B-Tree**, not a flat file or in-memory map; every `SELECT` and `INSERT` is a tree traversal.
- Full **leaf-node and internal-node splitting** on overflow, including root-split promotion.
- **Internal-node splitting and rebalancing** for sustained inserts, keeping tree height logarithmic as the table grows to 100K+ rows.
- Leaf nodes are linked (`next_leaf` pointers) for efficient sequential full-table scans without re-traversing the tree.
- Primary-key point lookups (`SELECT ... WHERE id = X`) use the B-Tree index directly, with binary search within nodes plus tree descent, giving the verified O(log n) behavior shown below.
- `UPDATE` and `DELETE` currently operate as a full-table cursor scan applying the `WHERE` predicate row by row. Index-accelerated point lookups for these are a natural next step, since the B-Tree machinery is already in place.

### 3. Custom Buffer Pool Manager (Pager)
- **Fixed 64MB buffer pool** (4KB pages, 16,384 frames); the engine never lets the OS blindly cache unbounded pages, it manages its own frame table.
- **Clock-sweep (second-chance) eviction algorithm** with per-frame reference bits and pin counts, so hot pages survive eviction pressure while cold pages are reclaimed.
- Manual **pin/unpin discipline** across every B-Tree and executor code path to guarantee no page is evicted while in use.

### 4. Write-Ahead Logging (WAL) & Crash Recovery
- Every mutating statement runs inside an explicit `beginTransaction()` / `commitTransaction()` / `rollbackTransaction()` boundary at the pager level.
- Dirty pages are logged to a `.wal` file **before** being applied to the main database file, following classic WAL durability rules.
- On startup, the pager **replays any uncommitted WAL entries** to restore a consistent on-disk state after an unclean shutdown.
- Failed transactions trigger **in-memory rollback** (frames reloaded from disk) without corrupting the base file.

### 5. Catalog Manager
- Table schemas (column names, types, primary-key flags, nullability) are serialized and persisted to a `catalog.meta` file, decoupled from row storage.
- Reloads the full catalog and reconstructs `Table` objects on process restart, so tables and their constraints survive across sessions.

### 6. Interactive REPL
- Standalone CLI (`psql`) with a `psql >>` prompt, meta-commands (`.exit`, `.help`), and formatted result-set output (headers and rows).

---

## Supported SQL Syntax

```sql
CREATE TABLE users (
    id INT PRIMARY_KEY,
    name STRING NOT_NULL,
    age INT
);

INSERT INTO users VALUES (1, 'Alice', 22);

SELECT * FROM users;
SELECT name, age FROM users WHERE id = 1;

UPDATE users SET age = 23 WHERE id = 1;

DELETE FROM users WHERE id = 1;

DROP TABLE users;
```

| Category        | Support                                      |
|------------------|-----------------------------------------------|
| DDL              | `CREATE TABLE`, `DROP TABLE`                  |
| DML              | `INSERT`, `SELECT`, `UPDATE`, `DELETE`        |
| Types            | `INT`, `FLOAT`, `CHAR`, `STRING`              |
| Constraints      | `PRIMARY_KEY`, `NOT_NULL`                     |
| Predicates       | Single-column `WHERE col = value`             |
| Scope            | Single-table queries only (no `JOIN`s)        |

---

## Performance (Benchmarked, not estimated)

All figures below were measured via the included benchmark suite (`tests/`), run against real disk-backed tables with the full B-Tree, pager, and WAL stack active (not an in-memory mock).

| Operation                     | Mean Latency | Throughput           |
|-------------------------------|--------------|----------------------|
| Sequential `INSERT`           | **~39 µs**   | **~25,000 ops/sec**  |
| Point `SELECT` (`WHERE id=X`) | **~11 µs**   | **~88,000 ops/sec**  |
| Random-order `INSERT`         | ~40 µs       | ~25,000 ops/sec      |
| Full table scan (20K rows)    | ~20.3 ms     | ~49 ops/sec          |

### Algorithmic Scaling: Verified O(log n) Point Lookups
Point `SELECT` latency was measured at 10 checkpoints as the table grew from 10,000 to 100,000 rows, directly exercising the B-Tree index rather than a scan:

| Table Size | Point SELECT Mean Latency |
|-----------:|---------------------------:|
| 10,000     | 11.45 µs                   |
| 50,000     | 11.71 µs                   |
| 100,000    | 11.91 µs                   |

Latency stays effectively flat across a 10x growth in table size, which is direct empirical confirmation that the B-Tree indexing scheme delivers logarithmic-time lookups rather than degrading toward a linear scan. `INSERT` throughput holds similarly steady (~40µs) across the same 10x growth, since B-Tree height only grows logarithmically as leaves split and internal nodes rebalance.

### Point DELETE and Reinsert
Deleting and immediately reinserting a row is a common pattern for testing whether an engine reclaims space cleanly. PSQL handles both sides of that cycle well:

| Operation                        | Mean Latency | Throughput          |
|-----------------------------------|--------------|----------------------|
| Point `DELETE` (`WHERE id = X`)   | ~5.97 ms     | ~167 ops/sec         |
| Reinsert after delete             | **~37 µs**   | **~26,900 ops/sec**  |

Reinsert latency lands right back in line with a fresh sequential insert (~37µs vs. ~39µs baseline), showing that deleted slots are reused efficiently rather than leaving the tree in a degraded state.

### Mixed Workload & Stress Testing
Beyond isolated CRUD benchmarks, PSQL was pushed through a YCSB-style mixed workload (50% read / 20% insert / 20% update / 10% delete) and several targeted stress scenarios, all against the full disk-backed engine:

- **Read throughput under mixed load**: **~84,600 ops/sec** (mean 11.82µs) for `SELECT` reads interleaved with concurrent inserts, updates, and deletes, essentially matching isolated point-SELECT performance even while the table is being actively mutated.
- **Catalog scale**: created **200 tables** in a single session, averaging **528µs per `CREATE TABLE`**, with cross-table inserts (randomly targeting any of the 200 tables) averaging **160µs**, ~6,200 ops/sec, showing catalog lookup overhead stays low even as the number of tracked tables grows.
- **Constraint-violation storm**: 3,000 back-to-back rejected inserts for each of duplicate primary keys, `NOT_NULL` violations, and malformed syntax. Syntax errors were rejected at **~99,200 ops/sec** and `NOT_NULL` violations at **~68,900 ops/sec**, confirming the error path is cheap and doesn't degrade engine performance under abuse. A sanity `SELECT *` immediately after the storm returned exactly the expected row, confirming no state corruption from the failed transactions.
- **Wide-row payloads**: insert latency scaled gracefully from 16-byte to 4096-byte string payloads (175µs to 349µs mean), with no errors at any payload size, exercising the pager's handling of larger row footprints within a page.
- **Sustained throughput**: **~5,990 inserts/sec** held continuously over a 5-second uninterrupted run (29,933 rows inserted), demonstrating stable long-run performance rather than a benchmark-only burst.

### Memory Safety
- **Zero heap leaks at exit**, confirmed via `valgrind --leak-check=full`: *"All heap blocks were freed i.e. no leaks are possible."* Verified across 433,875 total allocations and frees during the instrumented run.
- RSS held stable (under 1% drift) across 20 repeated full database lifecycle cycles (create, populate, drain, destroy).
- ResultSet allocation churn (3,000 repeated `SELECT *` calls) showed **0% RSS growth**, ruling out leaks in query-result construction.
- Sustained throughput of **~6,000 inserts/sec** maintained continuously over a 5-second stress run with no degradation.

---

## Build & Run

Built with a standard `Makefile` and no CMake, no external dependencies beyond the C++17 standard library.

```bash
# Build the executable and the static library
make

# Run the interactive REPL
./psql
psql >> CREATE TABLE users (id INT PRIMARY_KEY, name STRING NOT_NULL);
psql >> INSERT INTO users VALUES (1, 'Alice');
psql >> SELECT * FROM users;

# Clean build artifacts and generated database files
make clean
```

`make` produces two artifacts:
- **`psql`**: standalone interactive executable
- **`libpsql.a`**: static library exposing `PSQLDatabase::executeQuery()`, embeddable in other C++ projects

---

## Using PSQL as a Library in Your Own Project

Since `make` produces `libpsql.a` alongside the CLI, you can drop PSQL straight into another C++ project without touching the REPL, the source tree, or any internal headers. All you need is the static library and the single public header, `include/psql.h`.

### 1. Copy the two files you need

```bash
cp libpsql.a /path/to/your/project/
cp include/psql.h /path/to/your/project/
```

### 2. Include the header and use the `PSQLDatabase` class

```cpp
#include "psql.h"
#include <iostream>

int main() {
    PSQLDatabase db;

    db.executeQuery("CREATE TABLE users (id INT PRIMARY_KEY, name STRING NOT_NULL);");
    db.executeQuery("INSERT INTO users VALUES (1, 'Alice');");

    ResultSet result = db.executeQuery("SELECT * FROM users;");

    if (result.success) {
        for (const auto& row : result.rows) {
            for (const auto& val : row.values) {
                std::cout << val << " | ";
            }
            std::cout << "\n";
        }
    } else {
        std::cerr << "Query failed: " << result.errorMessage << "\n";
    }

    return 0;
}
```

`psql.h` exposes just two things: the `PSQLDatabase` class, with a single `executeQuery(const std::string&)` entry point, and the `ResultSet` / `Row` structs used to return results. Everything else (the lexer, parser, executor, B-Tree, and pager) stays hidden behind a `pimpl`, so your project never needs to see or compile PSQL's internals.

### 3. Compile and link against the static library

```bash
g++ -std=c++17 main.cpp -L. -lpsql -o my_app
./my_app
```

If `libpsql.a` and `psql.h` live somewhere other than your current directory, point the compiler at them explicitly:

```bash
g++ -std=c++17 main.cpp -I/path/to/psql/include -L/path/to/psql -lpsql -o my_app
```

That's the entire integration surface. PSQL will create its own `data/` directory (containing the per-table `.db` files, `.wal` logs, and `catalog.meta`) relative to wherever the embedding application runs, and everything persists across runs the same way it does with the standalone REPL.

### Running the benchmark suite

```bash
cp include/psql.h libpsql.a tests/
cd tests
./run_all_benchmarks.sh              # latency, scaling, memory, mixed-workload
./run_all_benchmarks.sh --with-valgrind   # adds a full memcheck pass
```

Results are written to `tests/results/`, including a consolidated `combined_metrics.csv` for further analysis.

---

## Why This Project

Most applications treat the database as a black box. PSQL was built to open that box, to implement, from first principles, the mechanisms that make databases both **fast** (B-Tree indexing, a self-managed buffer pool) and **correct under failure** (write-ahead logging, transactional rollback). Every component, the lexer, parser, executor, B-Tree, pager, and catalog, was designed and implemented independently, then benchmarked to validate that the resulting system actually behaves the way database theory predicts.
