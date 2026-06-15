#!/usr/bin/env python3
import heapq
import sys
from collections import Counter, defaultdict
from pathlib import Path

TOP_N = 20

SUMS = [
    "inserts", "insert_fasts", "removes", "replaces", "resizes", "reserves",
    "search_hashes", "search_hash_candidates",
    "search_buckets", "search_bucket_candidates",
    "iter_firsts", "iter_nexts",
]
SUM_POS = {field: i for i, field in enumerate(SUMS)}

TOPS = {
    "peak_n": ("peak_n", "init_where"),
    "inserts": ("inserts", "first_insert_where"),
    "insert_fasts": ("insert_fasts", "first_insert_fast_where"),
    "hash_candidates": ("search_hash_candidates", "first_search_hash_where"),
    "bucket_candidates": ("search_bucket_candidates", "first_search_bucket_where"),
    "iter_nexts": ("iter_nexts", "first_iter_where"),
}

FIELDS = {"program", "id", "peak_n"}
for field in SUMS:
    FIELDS.add(field)
for field, site in TOPS.values():
    FIELDS.add(field)
    FIELDS.add(site)


def stat_files(args):
    for arg in args:
        path = Path(arg)
        if path.is_dir():
            yield from sorted(path.glob("hmap-stats.*.csv"))
        else:
            yield path


def push_top(tops, name, score, program, id_, peak_n, site):
    if not score:
        return
    item = (score, program, id_, peak_n, site)
    heapq.heappush(tops[name], item)
    if len(tops[name]) > TOP_N:
        heapq.heappop(tops[name])


def add_site(sites, name, site, value):
    if site and value:
        sites[name][site] += value


def print_counter(title, counter):
    print(f"\n{title}")
    for key, value in counter.most_common(TOP_N):
        print(f"{value:>14}  {key}")


def main():
    if len(sys.argv) < 2:
        sys.exit(f"usage: {sys.argv[0]} STATS_DIR_OR_CSV [...]")

    rows = 0
    programs = defaultdict(Counter)
    sites = defaultdict(Counter)
    tops = {name: [] for name in TOPS}

    for path in stat_files(sys.argv[1:]):
        with path.open() as f:
            idx = {name: i for i, name in enumerate(next(f).rstrip("\n").split(","))
                   if name in FIELDS}
            program_i = idx["program"]
            id_i = idx["id"]
            peak_i = idx["peak_n"]
            sum_i = [idx[field] for field in SUMS]
            top_i = [(name, field, idx[site_field])
                     for name, (field, site_field) in TOPS.items()]
            for line in f:
                row = line.rstrip("\n").split(",")
                rows += 1
                program = row[program_i]
                peak_n = int(row[peak_i] or 0)
                vals = [int(row[i] or 0) for i in sum_i]
                counts = programs[program]
                counts["hmaps"] += 1
                counts["peak_n"] = max(counts["peak_n"], peak_n)
                for field, value in zip(SUMS, vals):
                    counts[field] += value

                add_site(sites, "inserts", row[idx["first_insert_where"]],
                         vals[SUM_POS["inserts"]])
                add_site(sites, "insert_fasts", row[idx["first_insert_fast_where"]],
                         vals[SUM_POS["insert_fasts"]])
                add_site(sites, "hash_candidates", row[idx["first_search_hash_where"]],
                         vals[SUM_POS["search_hash_candidates"]])
                add_site(sites, "bucket_candidates", row[idx["first_search_bucket_where"]],
                         vals[SUM_POS["search_bucket_candidates"]])
                add_site(sites, "iter_nexts", row[idx["first_iter_where"]],
                         vals[SUM_POS["iter_nexts"]])

                for name, field, site_i in top_i:
                    score = peak_n if field == "peak_n" else vals[SUM_POS[field]]
                    push_top(tops, name, score, program, row[id_i], peak_n, row[site_i])

    print(f"rows: {rows}")
    print("\nprograms")
    fields = ["hmaps", "peak_n"] + SUMS
    print("program," + ",".join(fields))
    for program, counts in sorted(programs.items()):
        print(program + "," + ",".join(str(counts[field]) for field in fields))

    for name, heap in tops.items():
        print(f"\ntop {name}")
        for score, program, id_, peak_n, site in sorted(heap, reverse=True):
            print(f"{score:>14}  {program:<12} id={id_:<8} peak={peak_n:<8} {site}")

    for name, counter in sites.items():
        print_counter(f"top sites: {name}", counter)


if __name__ == "__main__":
    main()
