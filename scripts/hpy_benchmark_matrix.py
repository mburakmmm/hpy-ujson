#!/usr/bin/env python3
from __future__ import annotations

import argparse
from datetime import date
import gc
import json
import platform
import random
import statistics
import sys
import timeit
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import ujson_hpy

try:
    import ujson
except Exception:  # pragma: no cover - optional comparator
    ujson = None


SMALL_PAYLOAD = {
    "ok": True,
    "answer": 42,
    "items": [1, 2, 3],
    "name": "hpy-ujson",
}
MEDIUM_PAYLOAD = {
    "id": 7,
    "name": "hpy-ujson",
    "ok": True,
    "score": 31.25,
    "tags": ["json", "hpy", "universal", "host", "bridge"],
    "nested": [
        {"k": "alpha", "v": 1},
        {"k": "beta", "v": 2},
        {"k": "gamma", "v": [1, 2, 3, 4]},
    ],
}
LARGE_INT_LIST = list(range(1024))
LARGE_RECORDS = {
    "records": [
        {
            "id": i,
            "name": f"user-{i}",
            "active": (i % 2 == 0),
            "score": i * 1.25,
            "tags": ["alpha", "beta", "gamma", str(i)],
            "meta": {"group": i % 4, "enabled": True, "count": i * 3},
        }
        for i in range(128)
    ]
}

REPO_ROOT = Path(__file__).resolve().parents[1]
COMPREHENSIVE_TEXT = (REPO_ROOT / "tests" / "comprehensive.json").read_text(
    encoding="utf-8"
)
COMPREHENSIVE_OBJECT = json.loads(COMPREHENSIVE_TEXT)

SMALL_TEXT = json.dumps(SMALL_PAYLOAD, separators=(",", ":"))
MEDIUM_TEXT = json.dumps(MEDIUM_PAYLOAD, separators=(",", ":"))
LARGE_INT_TEXT = json.dumps(LARGE_INT_LIST, separators=(",", ":"))
LARGE_RECORDS_TEXT = json.dumps(LARGE_RECORDS, separators=(",", ":"))
COMPREHENSIVE_BYTES = COMPREHENSIVE_TEXT.encode("utf-8")
COMPREHENSIVE_BYTEARRAY = bytearray(COMPREHENSIVE_BYTES)


@dataclass(frozen=True)
class BenchmarkCase:
    name: str
    loops: int
    json_cb: Callable[[], object]
    hpy_cb: Callable[[], object]
    ujson_cb: Callable[[], object] | None


def compact_json_dumps(value: object) -> str:
    return json.dumps(value, separators=(",", ":"))


def measure_once(callback: Callable[[], object], loops: int) -> float:
    timer = timeit.Timer(callback)
    was_enabled = gc.isenabled()
    gc.disable()
    try:
        return timer.timeit(number=loops)
    finally:
        if was_enabled:
            gc.enable()


def calibrate(callback: Callable[[], object], loops_hint: int, min_time: float) -> int:
    loops = min(loops_hint, 1_000)
    elapsed = measure_once(callback, loops)
    calibration_time = min(0.01, min_time / 4)

    while elapsed < calibration_time and loops < loops_hint:
        loops = min(loops * 2, loops_hint)
        elapsed = measure_once(callback, loops)

    if elapsed <= 0:
        return loops_hint
    return max(1, int(loops * min_time / elapsed))


def measure_case(
    callbacks: dict[str, Callable[[], object]],
    loops: int,
    repeat: int,
    min_time: float,
    rng: random.Random,
) -> dict[str, tuple[float, float]]:
    calibrated = {
        name: calibrate(callback, loops, min_time)
        for name, callback in callbacks.items()
    }
    samples: dict[str, list[float]] = {name: [] for name in callbacks}

    for _ in range(repeat):
        order = list(callbacks)
        rng.shuffle(order)
        for name in order:
            elapsed = measure_once(callbacks[name], calibrated[name])
            samples[name].append(calibrated[name] / elapsed)

    results = {}
    for name, rates in samples.items():
        median = statistics.median(rates)
        spread = (max(rates) - min(rates)) / median if median else 0.0
        results[name] = (median, spread)
    return results


def render_row(label: str, values: list[str]) -> str:
    return "| " + " | ".join([label] + values) + " |"


def build_cases() -> list[BenchmarkCase]:
    return [
        BenchmarkCase(
            name="loads small object",
            loops=250000,
            json_cb=lambda: json.loads(SMALL_TEXT),
            hpy_cb=lambda: ujson_hpy.loads(SMALL_TEXT),
            ujson_cb=(lambda: ujson.loads(SMALL_TEXT)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="dumps small object",
            loops=120000,
            json_cb=lambda: compact_json_dumps(SMALL_PAYLOAD),
            hpy_cb=lambda: ujson_hpy.dumps(SMALL_PAYLOAD),
            ujson_cb=(lambda: ujson.dumps(SMALL_PAYLOAD)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="loads medium object",
            loops=120000,
            json_cb=lambda: json.loads(MEDIUM_TEXT),
            hpy_cb=lambda: ujson_hpy.loads(MEDIUM_TEXT),
            ujson_cb=(lambda: ujson.loads(MEDIUM_TEXT)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="dumps medium object",
            loops=60000,
            json_cb=lambda: compact_json_dumps(MEDIUM_PAYLOAD),
            hpy_cb=lambda: ujson_hpy.dumps(MEDIUM_PAYLOAD),
            ujson_cb=(lambda: ujson.dumps(MEDIUM_PAYLOAD)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="dumps medium object sort_keys",
            loops=40000,
            json_cb=lambda: json.dumps(MEDIUM_PAYLOAD, sort_keys=True, separators=(",", ":")),
            hpy_cb=lambda: ujson_hpy.dumps(MEDIUM_PAYLOAD, sort_keys=True),
            ujson_cb=(lambda: ujson.dumps(MEDIUM_PAYLOAD, sort_keys=True)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="loads comprehensive fixture",
            loops=30000,
            json_cb=lambda: json.loads(COMPREHENSIVE_TEXT),
            hpy_cb=lambda: ujson_hpy.loads(COMPREHENSIVE_TEXT),
            ujson_cb=(lambda: ujson.loads(COMPREHENSIVE_TEXT)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="loads comprehensive bytes",
            loops=30000,
            json_cb=lambda: json.loads(COMPREHENSIVE_BYTES),
            hpy_cb=lambda: ujson_hpy.loads(COMPREHENSIVE_BYTES),
            ujson_cb=(lambda: ujson.loads(COMPREHENSIVE_BYTES)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="loads comprehensive bytearray",
            loops=30000,
            json_cb=lambda: json.loads(COMPREHENSIVE_BYTEARRAY),
            hpy_cb=lambda: ujson_hpy.loads(COMPREHENSIVE_BYTEARRAY),
            ujson_cb=(lambda: ujson.loads(COMPREHENSIVE_BYTEARRAY)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="dumps comprehensive fixture",
            loops=20000,
            json_cb=lambda: compact_json_dumps(COMPREHENSIVE_OBJECT),
            hpy_cb=lambda: ujson_hpy.dumps(COMPREHENSIVE_OBJECT),
            ujson_cb=(lambda: ujson.dumps(COMPREHENSIVE_OBJECT)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="loads 1024-int array",
            loops=60000,
            json_cb=lambda: json.loads(LARGE_INT_TEXT),
            hpy_cb=lambda: ujson_hpy.loads(LARGE_INT_TEXT),
            ujson_cb=(lambda: ujson.loads(LARGE_INT_TEXT)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="dumps 1024-int array",
            loops=40000,
            json_cb=lambda: compact_json_dumps(LARGE_INT_LIST),
            hpy_cb=lambda: ujson_hpy.dumps(LARGE_INT_LIST),
            ujson_cb=(lambda: ujson.dumps(LARGE_INT_LIST)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="loads 128-record object",
            loops=12000,
            json_cb=lambda: json.loads(LARGE_RECORDS_TEXT),
            hpy_cb=lambda: ujson_hpy.loads(LARGE_RECORDS_TEXT),
            ujson_cb=(lambda: ujson.loads(LARGE_RECORDS_TEXT)) if ujson is not None else None,
        ),
        BenchmarkCase(
            name="dumps 128-record object",
            loops=8000,
            json_cb=lambda: compact_json_dumps(LARGE_RECORDS),
            hpy_cb=lambda: ujson_hpy.dumps(LARGE_RECORDS),
            ujson_cb=(lambda: ujson.dumps(LARGE_RECORDS)) if ujson is not None else None,
        ),
    ]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a broader benchmark matrix for ujson_hpy."
    )
    parser.add_argument("--repeat", type=int, default=7)
    parser.add_argument("--min-time", type=float, default=0.15)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--show-spread", action="store_true")
    parser.add_argument(
        "--filter",
        action="append",
        default=[],
        help="Run cases whose names contain this text; may be repeated.",
    )
    args = parser.parse_args()

    if args.repeat < 3:
        parser.error("--repeat must be at least 3")
    if args.min_time <= 0:
        parser.error("--min-time must be positive")

    cases = build_cases()
    if args.filter:
        filters = [value.casefold() for value in args.filter]
        cases = [
            case
            for case in cases
            if any(value in case.name.casefold() for value in filters)
        ]
        if not cases:
            parser.error("--filter did not match any benchmark cases")

    libraries = ["json", "ujson_hpy"]
    if ujson is not None:
        libraries.append("ujson")

    print(f"Date: {date.today().isoformat()}")
    print(f"Machine: {platform.platform()}")
    print(f"Python: {platform.python_implementation()} {sys.version.split()[0]}")
    print(f"ujson_hpy ABI: {ujson_hpy._hpy_abi()}")
    print(f"Repeat: {args.repeat}")
    print(f"Minimum sample time: {args.min_time:.2f}s")
    print("Statistic: median ops/s; deterministic shuffled library order")
    print()
    print(render_row("benchmark", libraries))
    print(render_row("---", ["---"] * len(libraries)))

    rng = random.Random(args.seed)
    for case in cases:
        callbacks = {
            "json": case.json_cb,
            "ujson_hpy": case.hpy_cb,
            "ujson": case.ujson_cb,
        }
        active_callbacks = {
            name: callback
            for name, callback in callbacks.items()
            if callback is not None
        }
        results = measure_case(
            active_callbacks,
            case.loops,
            args.repeat,
            args.min_time,
            rng,
        )
        if args.show_spread:
            values = [
                f"{results[lib][0]:,.0f} ops/s ({results[lib][1] * 100:.1f}% span)"
                for lib in libraries
            ]
        else:
            values = [f"{results[lib][0]:,.0f} ops/s" for lib in libraries]
        print(render_row(case.name, values))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
