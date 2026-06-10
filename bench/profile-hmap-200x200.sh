#!/usr/bin/env bash
set -euo pipefail

branch=$(git branch --show-current)
if test -z "$branch"; then
    branch=$(git rev-parse --short HEAD)
fi
branch=${branch//\//-}
results_dir="results/${branch}-perf-hmap-200x200"

rm -rf "$results_dir"
mkdir -p "$results_dir"

git rev-parse HEAD > "$results_dir/git-head.txt"
git status --short > "$results_dir/git-status.txt"

perf record -F 99 -g --call-graph dwarf \
    -o "$results_dir/perf.data" \
    -- make check-perf TESTSUITEFLAGS="--rebuild 1" 2>&1 \
    | tee "$results_dir/check-perf.log"

cp tests/perf-testsuite.dir/results "$results_dir/results.txt"
cp tests/perf-testsuite.log "$results_dir/perf-testsuite.log"

perf report \
    -i "$results_dir/perf.data" \
    --stdio \
    --inline \
    --no-children \
    --percent-limit 0 \
    --symbol-filter=hmap \
    > "$results_dir/hmap-symbol-report.txt"
