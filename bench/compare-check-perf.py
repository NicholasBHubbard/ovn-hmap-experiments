#!/usr/bin/env python3
import html
import os
import re
import sys


METRICS = [
    ("Build NB", "Average (northd-loop in msec)", "build: northd loop avg"),
    ("Build NB", "Average (lflows in msec)", "build: lflows avg"),
    ("Measure northd recompute", "Average (northd-loop in msec)", "recompute: northd loop avg"),
    ("Measure northd recompute", "Average (lflows in msec)", "recompute: lflows avg"),
]


def read_results(path):
    section = None
    values = {}
    for line in open(path, encoding="utf-8"):
        match = re.search(r"Results for '([^']+)'", line)
        if match:
            section = match.group(1)
            continue
        match = re.search(r"^\s*([^:]+):\s*([0-9.]+)\s*$", line)
        if section and match:
            values[(section, match.group(1))] = float(match.group(2))
    return values


def result_name(path):
    name = os.path.basename(os.path.dirname(path))
    return name.removesuffix("-check-perf-200x200")


def rect(x, y, w, h, fill):
    return f'<rect x="{x}" y="{y}" width="{w:.1f}" height="{h}" fill="{fill}"/>'


def text(x, y, value, size=13, anchor="start", weight="400"):
    return (f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" '
            f'font-weight="{weight}" font-family="sans-serif">{html.escape(value)}</text>')


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--help":
        print("usage: compare-check-perf.py BASELINE_RESULTS EXPERIMENT_RESULTS")
        print()
        print("Writes results/comparison.svg.")
        return
    if len(sys.argv) != 3:
        raise SystemExit("usage: compare-check-perf.py BASELINE_RESULTS EXPERIMENT_RESULTS")

    baseline = read_results(sys.argv[1])
    experiment = read_results(sys.argv[2])
    baseline_name = result_name(sys.argv[1])
    experiment_name = result_name(sys.argv[2])
    rows = []
    for section, metric, label in METRICS:
        try:
            rows.append((label, baseline[(section, metric)], experiment[(section, metric)]))
        except KeyError as e:
            raise SystemExit(f"missing metric: {e}") from None

    max_value = max(max(base, exp) for _, base, exp in rows) or 1
    width, left, top, scale_width = 920, 250, 70, 520
    height = top + len(rows) * 70 + 60
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        text(width / 2, 32, f"{baseline_name} vs {experiment_name}", 20, "middle", "700"),
        rect(left, 48, 14, 14, "#6b7280"),
        text(left + 20, 60, baseline_name),
        rect(left + 110, 48, 14, 14, "#2563eb"),
        text(left + 130, 60, experiment_name),
    ]

    for i, (label, base, exp) in enumerate(rows):
        y = top + i * 70
        base_w = base / max_value * scale_width
        exp_w = exp / max_value * scale_width
        ratio = exp / base if base else 0
        out += [
            text(20, y + 21, label, 13),
            rect(left, y, base_w, 18, "#6b7280"),
            rect(left, y + 24, exp_w, 18, "#2563eb"),
            text(left + base_w + 8, y + 14, f"{base:.0f} ms"),
            text(left + exp_w + 8, y + 38, f"{exp:.0f} ms ({ratio:.2f}x)"),
        ]

    out.append("</svg>")
    os.makedirs("results", exist_ok=True)
    open("results/comparison.svg", "w", encoding="utf-8").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
