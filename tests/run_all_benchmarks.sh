#!/usr/bin/env bash
# run_all_benchmarks.sh
#
# Builds and runs the full PSQLDatabase benchmark suite:
#   1. bench_latency_throughput  - per-op latency percentiles + throughput
#   2. bench_scaling              - latency vs table size (B-tree complexity check)
#   3. bench_memory                - RSS growth / leak smoke test
#   4. bench_mixed_workload        - YCSB-style mixed CRUD, catalog stress, error-path, payload size
#   5. (optional) valgrind --leak-check=full pass over a short workload
#
# IMPORTANT: PSQLDatabase persists tables to disk under ./data/. This script
# wipes ./data/ before each binary so runs are reproducible and don't hit
# "Table already exists" errors from a previous run.
#
# Usage: ./run_all_benchmarks.sh [--with-valgrind]

set -e
cd "$(dirname "$0")"

echo "==================================================================="
echo " Building all benchmarks"
echo "==================================================================="
g++ -std=c++17 -O2 -no-pie bench_latency_throughput.cpp -L. -lpsql -o bench_latency_throughput
g++ -std=c++17 -O2 -no-pie bench_scaling.cpp             -L. -lpsql -o bench_scaling
g++ -std=c++17 -O2 -no-pie bench_memory.cpp              -L. -lpsql -o bench_memory
g++ -std=c++17 -O2 -no-pie bench_mixed_workload.cpp      -L. -lpsql -o bench_mixed_workload
echo "Build OK."
echo

mkdir -p results

echo "==================================================================="
echo " [1/4] Latency & Throughput Benchmark (N=20000)"
echo "==================================================================="
rm -rf data
./bench_latency_throughput 20000 | tee results/latency_throughput.txt
echo

echo "==================================================================="
echo " [2/4] Scaling Benchmark (max_n=100000, 10 checkpoints)"
echo "==================================================================="
rm -rf data
./bench_scaling 100000 10 | tee results/scaling.txt
echo

echo "==================================================================="
echo " [3/4] Memory Behavior Test (20 cycles x 5000 rows)"
echo "==================================================================="
rm -rf data
./bench_memory 20 5000 | tee results/memory.txt
echo

echo "==================================================================="
echo " [4/4] Mixed Workload Stress Test (sustained=5s)"
echo "==================================================================="
rm -rf data
./bench_mixed_workload 5 | tee results/mixed_workload.txt
echo

if [ "$1" == "--with-valgrind" ]; then
    echo "==================================================================="
    echo " [Extra] Valgrind memcheck pass (small workload, this is slow)"
    echo "==================================================================="
    rm -rf data
    g++ -std=c++17 -O0 -g -no-pie bench_memory.cpp -L. -lpsql -o bench_memory_dbg
    valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
        ./bench_memory_dbg 3 500 2>&1 | tee results/valgrind_memcheck.txt
    echo
fi

echo "==================================================================="
echo " All benchmarks complete. Raw output saved under ./results/"
echo "==================================================================="

# Extract all CSV lines across the runs into one consolidated file for
# spreadsheet import (resume portfolio / graphs).
{
    echo "# latency_throughput.csv rows"
    grep '^CSV,' results/latency_throughput.txt || true
    echo "# scaling.csv rows"
    grep '^CSV,' results/scaling.txt || true
    echo "# memory.csv rows"
    grep '^CSV,' results/memory.txt || true
} > results/combined_metrics.csv

echo "Consolidated CSV metrics written to results/combined_metrics.csv"
