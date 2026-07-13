#!/usr/bin/env python3
import argparse
import io
import json
import sys


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Exercise the built ujson_hpy artifact through an isolated import."
    )
    parser.add_argument(
        "--expected-abi",
        choices=("cpython", "hybrid", "universal"),
        required=True,
        help="Expected value returned by ujson_hpy._hpy_abi().",
    )
    args = parser.parse_args()

    import ujson_hpy

    actual_abi = ujson_hpy._hpy_abi()
    if actual_abi != args.expected_abi:
        raise AssertionError(
            f"Expected ABI {args.expected_abi!r}, got {actual_abi!r}"
        )

    payload = {
        "answer": 42,
        "flags": [True, False, None],
        "nested": {"alpha": 1, "beta": 2},
        "unicode": "merhaba dunya",
    }

    encoded = ujson_hpy.dumps(payload, sort_keys=True)
    decoded = ujson_hpy.loads(encoded)
    if decoded != {"answer": 42, "flags": [True, False, None], "nested": {"alpha": 1, "beta": 2}, "unicode": "merhaba dunya"}:
        raise AssertionError(f"Unexpected roundtrip result: {decoded!r}")

    if json.loads(encoded) != decoded:
        raise AssertionError("stdlib json and ujson_hpy disagree on the smoke payload")

    bytes_payload = b'{"bytes": [1, 2, 3], "ok": true}'
    if ujson_hpy.loads(bytes_payload) != {"bytes": [1, 2, 3], "ok": True}:
        raise AssertionError("bytes decode path failed")

    read_buffer = io.StringIO('{"stream": true, "items": [1, 2]}')
    if ujson_hpy.load(read_buffer) != {"stream": True, "items": [1, 2]}:
        raise AssertionError("load(fp) path failed")

    write_buffer = io.StringIO()
    result = ujson_hpy.dump({"stream": True, "items": [1, 2]}, write_buffer)
    if result is not None:
        raise AssertionError("dump(fp) should return None")
    if write_buffer.getvalue() != '{"stream":true,"items":[1,2]}':
        raise AssertionError("dump(fp) produced unexpected output")

    if ujson_hpy.dumps(b"abc", reject_bytes=False) != '"abc"':
        raise AssertionError("reject_bytes=False path failed")

    try:
        ujson_hpy.loads("{")
    except ujson_hpy.JSONDecodeError:
        pass
    else:
        raise AssertionError("invalid JSON did not raise JSONDecodeError")

    print("ujson_hpy external smoke check passed")
    print(f"abi={actual_abi}")
    print(f"version={ujson_hpy.__version__}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
