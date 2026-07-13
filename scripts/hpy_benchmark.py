#!/usr/bin/env python3
import argparse
import json
import platform
import sys
import timeit

import ujson_hpy

try:
    import ujson
except Exception:  # pragma: no cover - optional comparator
    ujson = None


PAYLOAD = {
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
DOCUMENT = json.dumps(PAYLOAD, separators=(",", ":"))


def calls_per_second(stmt: str, setup: str, loops: int, repeat: int) -> float:
    results = timeit.repeat(stmt, setup=setup, number=loops, repeat=repeat)
    return loops / min(results)


def render_row(label: str, values: list[str]) -> str:
    cells = [label] + values
    return "| " + " | ".join(cells) + " |"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Benchmark ujson_hpy against stdlib json and optional classic ujson."
    )
    parser.add_argument("--loops", type=int, default=50000)
    parser.add_argument("--repeat", type=int, default=7)
    args = parser.parse_args()

    libraries = ["json", "ujson_hpy"]
    if ujson is not None:
        libraries.append("ujson")

    setup = "from __main__ import DOCUMENT, PAYLOAD, json, ujson_hpy, ujson"

    benchmarks = [
        ("dumps compact", {
            "json": "json.dumps(PAYLOAD, separators=(\",\", \":\"))",
            "ujson_hpy": "ujson_hpy.dumps(PAYLOAD)",
            "ujson": "ujson.dumps(PAYLOAD)",
        }),
        ("loads compact", {
            "json": "json.loads(DOCUMENT)",
            "ujson_hpy": "ujson_hpy.loads(DOCUMENT)",
            "ujson": "ujson.loads(DOCUMENT)",
        }),
        ("dumps sort_keys", {
            "json": "json.dumps(PAYLOAD, sort_keys=True, separators=(\",\", \":\"))",
            "ujson_hpy": "ujson_hpy.dumps(PAYLOAD, sort_keys=True)",
            "ujson": "ujson.dumps(PAYLOAD, sort_keys=True)",
        }),
    ]

    print(f"Date: 2026-07-13")
    print(f"Machine: {platform.platform()}")
    print(f"Python: {platform.python_implementation()} {sys.version.split()[0]}")
    print(f"ujson_hpy ABI: {ujson_hpy._hpy_abi()}")
    print(f"Loops: {args.loops}, repeat: {args.repeat}")
    print()
    print(render_row("benchmark", libraries))
    print(render_row("---", ["---"] * len(libraries)))

    for label, cases in benchmarks:
        values = []
        for lib in libraries:
            cps = calls_per_second(cases[lib], setup, args.loops, args.repeat)
            values.append(f"{cps:,.0f} ops/s")
        print(render_row(label, values))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
