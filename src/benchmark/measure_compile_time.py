#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the compilation time of the Lightweight template-heavy benchmark.

This recompiles the single benchmark translation unit (benchmark.cpp, which
pulls in the deep Table99 relationship graph) in isolation and reports:

  * wall-clock compile time (median of N runs), and
  * a precise per-activity / per-template breakdown parsed from Clang's
    -ftime-trace output (frontend vs backend, time spent instantiating each
    class/function template).

It drives the *exact* compile command CMake recorded in compile_commands.json,
so the flags, include paths and standard match a real build. Re-run it before
and after a change to see whether recursive instantiation got cheaper.

Prerequisites:
  1. Configure a Clang build with the benchmark enabled, e.g.:
       cmake --preset clang-debug -DLIGHTWEIGHT_BUILD_BENCHMARK=ON
  2. Generate the entities once (see how_to.md).

Usage:
  python src/benchmark/measure_compile_time.py [--build-dir DIR] [--runs N] [--top N]
"""

import argparse
import json
import os
import shlex
import statistics
import subprocess
import sys
import tempfile
import time
from collections import defaultdict

# Clang time-trace event names that correspond to template instantiation work.
# We aggregate the per-symbol "detail" of these to find the hottest templates.
INSTANTIATION_EVENTS = {
    "InstantiateClass",
    "InstantiateFunction",
    "ParseClass",
    "ParseTemplate",
    "DebugType",
}


def find_benchmark_command(build_dir, source_override=None):
    """Return (compile_command_argv, cwd, source_path) for benchmark.cpp.

    If source_override is given, the recorded benchmark.cpp flags are reused but
    the input file is swapped — handy for A/B-ing compile-time variants without
    reconfiguring CMake.
    """
    cc_path = os.path.join(build_dir, "compile_commands.json")
    if not os.path.isfile(cc_path):
        sys.exit(f"error: {cc_path} not found. Configure the build first "
                 f"(cmake --preset ... -DLIGHTWEIGHT_BUILD_BENCHMARK=ON).")
    with open(cc_path) as f:
        entries = json.load(f)
    matches = [e for e in entries if e["file"].replace("\\", "/").endswith("benchmark/benchmark.cpp")]
    if not matches:
        sys.exit("error: benchmark.cpp is not in compile_commands.json. "
                 "Reconfigure with -DLIGHTWEIGHT_BUILD_BENCHMARK=ON.")
    entry = matches[0]
    argv = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    source = entry["file"]
    if source_override:
        override = os.path.abspath(source_override)
        argv = [override if a == source else a for a in argv]
        source = override
    return argv, entry.get("directory", build_dir), source


def build_profiling_command(argv, out_obj, trace_granularity):
    """Strip cached-output flags and force a fresh compile with -ftime-trace."""
    cleaned = []
    skip_next = False
    for arg in argv:
        if skip_next:
            skip_next = False
            continue
        if arg == "-o":
            skip_next = True
            continue
        if arg.startswith("-ftime-trace"):
            continue
        cleaned.append(arg)
    # -ftime-trace=PATH writes the JSON exactly where we want it.
    trace_json = out_obj + ".json"
    cleaned += [
        f"-ftime-trace={trace_json}",
        f"-ftime-trace-granularity={trace_granularity}",
        "-o", out_obj,
    ]
    return cleaned, trace_json


def time_compile(cmd, cwd, runs):
    """Run the compile `runs` times, returning (wall_times, last_returncode)."""
    times = []
    rc = 0
    for i in range(runs):
        start = time.perf_counter()
        proc = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        elapsed = time.perf_counter() - start
        rc = proc.returncode
        if rc != 0:
            sys.stderr.write(proc.stdout.decode(errors="replace"))
            sys.exit(f"error: compile failed (run {i + 1}/{runs}, rc={rc}).")
        times.append(elapsed)
    return times, rc


def parse_trace(trace_json):
    """Aggregate a Clang -ftime-trace file.

    Returns (totals, per_template) where:
      totals       maps high-level activity name -> microseconds (from the
                   "Total <Name>" summary events Clang emits), and
      per_template maps a template's detail string -> (total_us, count).
    """
    with open(trace_json) as f:
        trace = json.load(f)

    totals = {}
    per_template = defaultdict(lambda: [0, 0])
    for ev in trace.get("traceEvents", []):
        if ev.get("ph") != "X":
            continue
        name = ev.get("name", "")
        dur = ev.get("dur", 0)
        if name.startswith("Total "):
            totals[name[len("Total "):]] = dur
            continue
        if name in INSTANTIATION_EVENTS:
            detail = ev.get("args", {}).get("detail")
            if detail:
                key = (name, detail)
                per_template[key][0] += dur
                per_template[key][1] += 1
    return totals, per_template


def fmt_ms(us):
    return f"{us / 1000.0:9.1f} ms"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="out/build/clang-debug",
                        help="CMake build dir containing compile_commands.json "
                             "(default: out/build/clang-debug)")
    parser.add_argument("--runs", type=int, default=3,
                        help="number of timed compile runs; median is reported (default: 3)")
    parser.add_argument("--top", type=int, default=25,
                        help="how many hottest template instantiations to list (default: 25)")
    parser.add_argument("--granularity", type=int, default=100,
                        help="-ftime-trace-granularity in microseconds (default: 100)")
    parser.add_argument("--json-out", default=None,
                        help="optional path to dump the aggregated result as JSON")
    parser.add_argument("--source", default=None,
                        help="compile this source instead of benchmark.cpp, reusing its flags "
                             "(for A/B-ing compile-time variants)")
    args = parser.parse_args()

    argv, cwd, source = find_benchmark_command(args.build_dir, args.source)
    compiler = os.path.basename(argv[0])
    if "clang" not in compiler.lower():
        sys.exit(f"error: -ftime-trace needs Clang, but the build uses '{compiler}'. "
                 f"Use a clang preset (e.g. --build-dir out/build/clang-debug).")

    with tempfile.TemporaryDirectory(prefix="lw-ctbench-") as tmp:
        out_obj = os.path.join(tmp, "benchmark.o")
        cmd, trace_json = build_profiling_command(argv, out_obj, args.granularity)

        print(f"Source     : {source}")
        print(f"Compiler   : {argv[0]}")
        print(f"Runs       : {args.runs}\n")

        times, _ = time_compile(cmd, cwd, args.runs)
        totals, per_template = parse_trace(trace_json)

    wall_median = statistics.median(times)
    wall_min = min(times)
    print("== Wall-clock compile time ==")
    print(f"  median : {wall_median * 1000:9.1f} ms")
    print(f"  min    : {wall_min * 1000:9.1f} ms")
    print(f"  runs   : {', '.join(f'{t * 1000:.0f}' for t in times)} ms\n")

    print("== Compiler activity (from -ftime-trace) ==")
    for key in ["ExecuteCompiler", "Frontend", "Backend", "Source",
                "InstantiateClass", "InstantiateFunction", "PerformPendingInstantiations"]:
        if key in totals:
            print(f"  {key:<30} {fmt_ms(totals[key])}")
    print()

    ranked = sorted(per_template.items(), key=lambda kv: kv[1][0], reverse=True)
    print(f"== Top {args.top} template instantiations (by total time) ==")
    print(f"  {'total':>11}  {'count':>6}  {'avg':>9}  kind / symbol")
    for (kind, detail), (total_us, count) in ranked[:args.top]:
        avg = total_us / count if count else 0
        symbol = detail if len(detail) <= 90 else detail[:87] + "..."
        print(f"  {fmt_ms(total_us)}  {count:>6}  {fmt_ms(avg).strip():>9}  {kind}: {symbol}")

    if args.json_out:
        with open(args.json_out, "w") as f:
            json.dump({
                "wall_seconds": {"median": wall_median, "min": wall_min, "runs": times},
                "totals_us": totals,
                "top_templates": [
                    {"kind": k, "detail": d, "total_us": v[0], "count": v[1]}
                    for (k, d), v in ranked[:args.top]
                ],
            }, f, indent=2)
        print(f"\nWrote aggregated result to {args.json_out}")


if __name__ == "__main__":
    main()
