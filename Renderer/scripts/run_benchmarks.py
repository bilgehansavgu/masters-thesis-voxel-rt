#!/usr/bin/env python3
"""Runs the benchmark for a list of cases and writes one table from the results files.

A case is one scene: a voxel model, an encoding, a brick size and a camera orbit. The benchmark
(`thesis_renderer --benchmark`) runs every tracer on that scene and writes one results file for
each tracer. This script

  1. runs the benchmark once for each case of a plan file (JSON, see scripts/ablation_step3.json),
  2. puts the results files of the whole run into one folder, with the case in the file name,
  3. looks at the spread between the repeats in each results file. A tracer with a spread above
     the limit (3 %) is run again, up to `--attempts` times. The file with the high spread goes
     to the subfolder `rejected`,
  4. writes `table.md` and `table.csv` from the results files in the folder.

Every number in the table comes from a results file. The script measures nothing itself.

Use, from the folder `Renderer`:

    python scripts/run_benchmarks.py scripts/ablation_step3.json
    python scripts/run_benchmarks.py --table-only results/<folder>     # write the table again
    python scripts/run_benchmarks.py --output <dir> <plan> -- --repeats 3   # options of the benchmark after --

The options of this script come before the plan file: everything after the plan goes to the benchmark.

Do not build and do not use the GPU for other work while it runs.
"""

import argparse
import csv
import datetime
import json
import pathlib
import shutil
import subprocess
import sys

SPREAD_LIMIT_PERCENT = 3.0
RENDERER = pathlib.Path("build/src/app/Release/thesis_renderer.exe")

# One column of the table: (heading, function of one row).
COLUMNS = [
    ("model", lambda r: r["model"]),
    ("encoding", lambda r: r["encoding"]),
    ("brick", lambda r: r["brick_size"]),
    ("orbit", lambda r: r["orbit"]),
    ("tracer", lambda r: r["tracer"]),
    ("trace ms", lambda r: fixed(r.get("trace_ms"), 3)),
    ("above null ms", lambda r: fixed(r.get("above_null_ms"), 3)),
    ("spread %", lambda r: fixed(r.get("spread_percent"), 1)),
    ("Mrays/s", lambda r: fixed(r.get("mrays_per_second"), 0)),
    ("structure steps", lambda r: fixed(r.get("structure_steps"), 2)),
    ("brick tests", lambda r: fixed(r.get("primitive_tests"), 2)),
    ("mipmap cells", lambda r: fixed(r.get("mipmap_cells"), 2)),
    ("mipmap skips", lambda r: fixed(r.get("mipmap_skips"), 2)),
    ("cells", lambda r: fixed(r.get("cells"), 2)),
    ("runs", lambda r: fixed(r.get("runs"), 2)),
    ("structure MiB", lambda r: fixed(mib(r.get("structure_bytes")), 2)),
    ("build ms", lambda r: fixed(r.get("build_ms"), 1)),
    ("scene MiB", lambda r: fixed(mib(r.get("scene_bytes")), 1)),
    ("bits per voxel", lambda r: fixed(r.get("bits_per_voxel"), 2)),
    ("bricks", lambda r: r.get("primitives", "")),
    ("warm-up s", lambda r: fixed(r.get("warmup_seconds"), 1)),
    ("note", lambda r: r.get("note", "")),
]


def fixed(value, digits):
    return "" if value is None else f"{value:.{digits}f}"


def mib(value):
    return None if value is None else value / (1024.0 * 1024.0)


def case_id(case):
    return f"{case['model_name']}_{case['encoding']}_b{case['brick_size']}_{case['orbit']}"


def cases_of(plan):
    """The cases of a plan: every model x every orbit x every configuration."""
    cases = []
    for model in plan["models"]:
        for orbit, camera in plan["orbits"].items():
            for configuration in plan["configurations"]:
                cases.append({"model": model,
                              "model_name": pathlib.Path(model).stem,
                              "orbit": orbit,
                              "camera": camera,
                              "encoding": configuration["encoding"],
                              "brick_size": configuration["brick_size"]})
    return cases


def run_benchmark(case, tracers, incoming, extra):
    """Runs the benchmark for one case. Returns its console output."""
    command = [str(RENDERER), "--benchmark", "--model", case["model"], "--encoding", case["encoding"],
               "--brick-size", str(case["brick_size"]), "--camera", *[str(v) for v in case["camera"]],
               "--output", str(incoming), *extra]
    if tracers:
        command += ["--tracer", *tracers]
    done = subprocess.run(command, capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"the benchmark failed for {case_id(case)}:\n{done.stdout}\n{done.stderr}")
    return done.stdout


def spread_of(path):
    with open(path, encoding="utf-8") as file:
        return json.load(file)["summary"]["trace_ms"]["spread_between_repeats_percent"]


def run_case(case, folder, attempts, extra):
    """Runs one case until no tracer has a spread above the limit, or `attempts` times."""
    incoming = folder / "incoming"
    rejected = folder / "rejected"
    name = case_id(case)
    tracers = []  # empty: all tracers
    log = "# case " + json.dumps(case) + "\n"  # the first line names the case, for the table
    for attempt in range(1, attempts + 1):
        incoming.mkdir(parents=True, exist_ok=True)
        log += f"--- attempt {attempt} ---\n" + run_benchmark(case, tracers, incoming, extra)
        again = []
        for path in sorted(incoming.glob("*.json")):
            with open(path, encoding="utf-8") as file:
                tracer = json.load(file)["run"]["tracer"]
            target = folder / f"{name}_{tracer}.json"
            spread = spread_of(path)
            if spread > SPREAD_LIMIT_PERCENT and attempt < attempts:
                rejected.mkdir(exist_ok=True)
                shutil.move(str(path), str(rejected / f"{name}_{tracer}_attempt{attempt}.json"))
                again.append(tracer)
                print(f"    {tracer}: spread {spread:.1f} % is above {SPREAD_LIMIT_PERCENT:.0f} %, runs again")
            else:
                shutil.move(str(path), str(target))
                (folder / f"{name}_{tracer}.attempts").write_text(str(attempt), encoding="utf-8")
        incoming.rmdir()
        if not again:
            break
        tracers = again
    (folder / f"{name}.log").write_text(log, encoding="utf-8")


def row_of(path):
    """One row of the table from one results file."""
    with open(path, encoding="utf-8") as file:
        data = json.load(file)
    run, summary = data["run"], data["summary"]
    stem = path.stem  # <model>_<encoding>_b<N>_<orbit>_<tracer>
    tracer = run["tracer"]
    scene = run["scene"]
    orbit = stem[: -len(tracer) - 1].rsplit("_", 1)[1]
    row = {
        "model": scene["name"],
        "encoding": scene["encoding"],
        "brick_size": scene["brick_size"],
        "orbit": orbit,
        "tracer": tracer,
        "trace_ms": summary["trace_ms"]["median_of_repeat_medians"],
        "spread_percent": summary["trace_ms"]["spread_between_repeats_percent"],
        "mrays_per_second": summary["mrays_per_second"],
        "structure_bytes": run["prepare"]["memory_bytes"],
        "build_ms": run["prepare"]["cpu_ms"] + run["prepare"]["gpu_ms"],
        "scene_bytes": scene["memory"]["total_bytes"],
        "bits_per_voxel": scene["memory"]["bits_per_voxel"],
        "primitives": scene["primitives"],
        "voxels": scene["voxels"],
        "warmup_seconds": run["warmup"]["seconds"],
        "commit": run["build"]["git_commit"][:7] + (" dirty" if run["build"]["git_dirty"] else ""),
        "device": f"{run['device']['name']}, driver {run['device']['driver_version']}",
        "buffers": {buffer["name"]: buffer["bytes"] for buffer in scene["memory"]["buffers"]},
    }
    work = summary.get("ray_work")
    if work:
        mean = work["mean_per_ray_with_hit"]
        row.update({"structure_steps": mean["structure_steps"], "primitive_tests": mean["primitive_tests"],
                    "mipmap_cells": mean["mipmap_cells"], "mipmap_skips": mean.get("mipmap_skips"), "cells": mean["cells"], "runs": mean["runs"],
                    "hit_percent": 100.0 * work["rays_with_hit"] / work["rays"]})
    notes = []
    attempts = path.with_suffix(".attempts")
    if attempts.exists() and attempts.read_text(encoding="utf-8") != "1":
        notes.append(f"attempt {attempts.read_text(encoding='utf-8')}")
    if row["spread_percent"] > SPREAD_LIMIT_PERCENT:
        notes.append(f"SPREAD ABOVE {SPREAD_LIMIT_PERCENT:.0f} %")
    if not (run["warmup"]["clock_settled"] and run["warmup"]["trace_settled"]):
        notes.append("warm-up not settled")
    row["note"] = ", ".join(notes)
    return row


def skipped_rows(folder):
    """Rows for the tracers that refused a scene: the benchmark prints `<tracer> skipped: <why>`."""
    rows = []
    for log in sorted(folder.glob("*.log")):
        lines = log.read_text(encoding="utf-8").splitlines()
        case = json.loads(lines[0].removeprefix("# case "))
        seen = set()
        for line in lines[1:]:
            parts = line.split(None, 2)
            if len(parts) == 3 and parts[1] == "skipped:" and parts[0] not in seen:
                seen.add(parts[0])
                rows.append({"model": case["model_name"], "encoding": case["encoding"],
                             "brick_size": case["brick_size"], "orbit": case["orbit"], "tracer": parts[0],
                             "note": "skipped: " + parts[2]})
    return rows


def write_table(folder):
    # Every .json of the folder except the copy of the plan is a results file.
    rows = [row_of(path) for path in sorted(folder.glob("*.json")) if path.name != "plan.json"]
    if not rows:
        sys.exit(f"{folder}: no results files")
    # The time above the null tracer of the same case.
    floor = {(r["model"], r["encoding"], r["brick_size"], r["orbit"]): r["trace_ms"]
             for r in rows if r["tracer"] == "null"}
    for row in rows:
        null_ms = floor.get((row["model"], row["encoding"], row["brick_size"], row["orbit"]))
        if null_ms is not None and row["tracer"] != "null":
            row["above_null_ms"] = row["trace_ms"] - null_ms
    commits = sorted({row["commit"] for row in rows})
    devices = sorted({row["device"] for row in rows})
    rows += skipped_rows(folder)
    rows.sort(key=lambda r: (r["model"], r["orbit"], r["brick_size"], r["encoding"], r["tracer"]))

    headings = [heading for heading, _ in COLUMNS]
    lines = [[str(value(row)) for _, value in COLUMNS] for row in rows]
    with open(folder / "table.csv", "w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(headings)
        writer.writerows(lines)

    flagged = [row for row in rows if "SPREAD" in row.get("note", "")]
    with open(folder / "table.md", "w", encoding="utf-8") as file:
        file.write(f"# Benchmark table of `{folder.name}`\n\n")
        file.write(f"Commit: {', '.join(commits)}. Machine: {', '.join(devices)}.\n\n")
        file.write("`trace ms` is the median of the repeat medians. `above null ms` is that time minus the time "
                   "of the null tracer on the same scene. The work columns (`structure steps` to `runs`) are means "
                   "for a ray that hits, from the counting variant of the shader. `build ms` is CPU plus GPU time "
                   "of the search structure.\n\n")
        if flagged:
            file.write(f"**{len(flagged)} row(s) have a spread between repeats above "
                       f"{SPREAD_LIMIT_PERCENT:.0f} % after all attempts. Do not use them.**\n\n")
        file.write("| " + " | ".join(headings) + " |\n")
        file.write("|" + "|".join("---" for _ in headings) + "|\n")
        for line in lines:
            file.write("| " + " | ".join(line) + " |\n")
    print(f"table: {folder / 'table.md'} and table.csv, {len(rows)} rows, {len(flagged)} flagged")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("plan", nargs="?", help="JSON file with models, orbits and configurations")
    parser.add_argument("--table-only", metavar="FOLDER", help="only write the table of a finished run again")
    parser.add_argument("--output", default="results", help="where the folder of the run is made")
    parser.add_argument("--attempts", type=int, default=3, help="runs of a tracer with a spread above the limit")
    parser.add_argument("extra", nargs=argparse.REMAINDER,
                        help="after `--`: more options for the benchmark, for example -- --repeats 3")
    arguments = parser.parse_args()

    if arguments.table_only:
        write_table(pathlib.Path(arguments.table_only))
        return
    if not arguments.plan:
        parser.error("give a plan file or --table-only")
    if not RENDERER.exists():
        sys.exit(f"{RENDERER} not found: build the Release configuration and start from the folder Renderer")

    with open(arguments.plan, encoding="utf-8") as file:
        plan = json.load(file)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    folder = pathlib.Path(arguments.output) / f"{stamp}_{plan['name']}"
    folder.mkdir(parents=True)
    shutil.copy(arguments.plan, folder / "plan.json")

    extra = [value for value in arguments.extra if value != "--"]
    cases = cases_of(plan)
    for index, case in enumerate(cases, 1):
        print(f"[{index}/{len(cases)}] {case_id(case)}", flush=True)
        run_case(case, folder, arguments.attempts, extra)
    write_table(folder)


if __name__ == "__main__":
    main()
