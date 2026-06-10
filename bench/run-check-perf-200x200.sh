#!/usr/bin/env bash
set -euo pipefail

no_parallel=false
case "${1:-}" in
    --no-parallel)
        no_parallel=true
        ;;
    "" )
        ;;
    -h|--help)
        echo "usage: $0 [--no-parallel]"
        exit 0
        ;;
    *)
        echo "usage: $0 [--no-parallel]" >&2
        exit 2
        ;;
esac

branch=$(git branch --show-current)
if test -z "$branch"; then
    branch=$(git rev-parse --short HEAD)
fi
branch=${branch//\//-}
results_dir="results/${branch}-check-perf-200x200"
if $no_parallel; then
    results_dir="${results_dir}-no-parallel"
fi

rm -rf "$results_dir"
mkdir -p "$results_dir"

git rev-parse HEAD > "$results_dir/git-head.txt"
git status --short > "$results_dir/git-status.txt"

if $no_parallel; then
    make tests/perf-testsuite
    backup=$(mktemp)
    cp tests/perf-testsuite "$backup"
    restore_perf_testsuite() {
        cp "$backup" tests/perf-testsuite
        rm -f "$backup"
    }
    trap restore_perf_testsuite EXIT

    perl -0pi -e '
        s/NORTHD_USE_PARALLELIZATION=yes\novs_init\n\novn_start\n/NORTHD_USE_PARALLELIZATION=yes\novs_init\n\novn_start\ncheck ovn-appctl -t northd\/ovn-northd parallel-build\/set-n-threads 1\n/ or die "failed to patch perf-testsuite\n"
    ' tests/perf-testsuite
    perl -0pi -e '
        s/(200 Hypervisors, 200 Logical Ports\/Hypervisor -- parallelization)=yes/$1=no/g
    ' tests/perf-testsuite
fi

make check-perf TESTSUITEFLAGS="--rebuild 1" 2>&1 \
    | tee "$results_dir/check-perf.log"

cp tests/perf-testsuite.dir/results "$results_dir/results.txt"
cp tests/perf-testsuite.log "$results_dir/perf-testsuite.log"
