# Benchmark Scripts

Run these commands from the repository root after configuring and building OVN.

The primary workload is OVN's built-in 200x200 northd scale test: 200
hypervisors with 200 logical ports per hypervisor.

Run the end-to-end benchmark:

```sh
./bench/run-check-perf-200x200.sh
```

Run the same benchmark with ovn-northd parallel lflow build disabled:

```sh
./bench/run-check-perf-200x200.sh --no-parallel
```

Run the same workload under `perf` and generate an hmap-focused report:

```sh
./bench/profile-hmap-200x200.sh
```

Compare two saved `results.txt` files:

```sh
./bench/compare-check-perf.py baseline-results.txt experiment-results.txt
```

Results are written under `results/`.
