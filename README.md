# hpy-ujson

[![PyPI version](https://img.shields.io/pypi/v/ujson.svg?logo=pypi&logoColor=FFE873)](https://pypi.org/project/ujson)
[![Supported Python versions](https://img.shields.io/pypi/pyversions/ujson.svg?logo=python&logoColor=FFE873)](https://pypi.org/project/ujson)
[![PyPI downloads](https://img.shields.io/pypi/dm/ujson.svg)](https://pypistats.org/packages/ujson)
[![GitHub Actions status](https://github.com/ultrajson/ultrajson/workflows/Test/badge.svg)](https://github.com/ultrajson/ultrajson/actions)
[![codecov](https://codecov.io/gh/ultrajson/ultrajson/branch/main/graph/badge.svg)](https://codecov.io/gh/ultrajson/ultrajson)
[![DOI](https://zenodo.org/badge/1418941.svg)](https://zenodo.org/badge/latestdoi/1418941)
[![Code style: Black](https://img.shields.io/badge/code%20style-Black-000000.svg)](https://github.com/psf/black)

`hpy-ujson` is an HPy-focused fork of UltraJSON. It keeps UltraJSON's C
encoder/decoder core, ports the Python binding layer to HPy, and produces a
usable `ujson_hpy` module for CPython ABI and Universal ABI hosts.

This repository is aimed at local builds, runtime integration, and performance
work. It is not yet published as a separate PyPI distribution.

## Project status

- The classic `ujson` extension is still buildable for side-by-side comparison.
- The HPy port lives in a separate module named `ujson_hpy`.
- Local validation currently includes:
  - classic suite: `476 passed, 40 skipped, 1 xfailed`
  - HPy CPython suite: `174 passed, 4 skipped`
  - HPy Universal suite: `178 passed`
  - HPy debug suite: `178 passed`
  - external smoke checks for CPython and Universal artifacts: passing
- The repository follows a fork-first model for HPy work. Upstream UltraJSON is
  still a useful code base reference, but this fork is where HPy-specific
  iteration, documentation, and host-integration work happen.

## Quick start

Create a local environment and install the development dependencies:

```sh
python -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip setuptools setuptools-scm pytest hpy tox
```

Build and validate the HPy artifacts:

```sh
./scripts/hpy-build.sh cpython
./scripts/hpy-build.sh universal

./scripts/hpy-test.sh cpython
./scripts/hpy-test.sh universal

./scripts/hpy-smoke.sh cpython
./scripts/hpy-smoke.sh universal
```

## Usage

The classic upstream-style module is still `ujson` when you build the CPython
extension. The HPy port is imported as `ujson_hpy`:

```pycon
>>> import ujson_hpy
>>> ujson_hpy.dumps([{"key": "value"}, 81, True])
'[{"key":"value"},81,true]'
>>> ujson_hpy.loads("""[{"key": "value"}, 81, true]""")
[{'key': 'value'}, 81, True]
```

If you also want the classic extension in the same build, set
`UJSON_BUILD_CPYTHON_EXT=1`.

### Encoder options

#### encode_html_chars

Used to enable special encoding of "unsafe" HTML characters into safer Unicode
sequences. Default is `False`:

```pycon
>>> ujson_hpy.dumps("<script>John&Doe", encode_html_chars=True)
'"\\u003cscript\\u003eJohn\\u0026Doe"'
```

#### ensure_ascii

Limits output to ASCII and escapes all extended characters above 127. Default is `True`.
If your end format supports UTF-8, setting this option to false is highly recommended to
save space:

```pycon
>>> ujson_hpy.dumps("åäö")
'"\\u00e5\\u00e4\\u00f6"'
>>> ujson_hpy.dumps("åäö", ensure_ascii=False)
'"åäö"'
```

#### escape_forward_slashes

Controls whether forward slashes (`/`) are escaped. Default is `True`:

```pycon
>>> ujson_hpy.dumps("https://example.com")
'"https:\\/\\/example.com"'
>>> ujson_hpy.dumps("https://example.com", escape_forward_slashes=False)
'"https://example.com"'
```

#### indent

Controls whether indentation ("pretty output") is enabled. Default is `0` (disabled):

```pycon
>>> ujson_hpy.dumps({"foo": "bar"})
'{"foo":"bar"}'
>>> print(ujson_hpy.dumps({"foo": "bar"}, indent=4))
{
    "foo": "bar"
}
```

## Benchmarks

*UltraJSON* calls/sec compared to other popular JSON parsers with performance gain
specified below each.

### Test machine

Linux 5.15.0-1037-azure x86_64 #44-Ubuntu SMP Thu Apr 20 13:19:31 UTC 2023

### Versions

- CPython 3.11.3 (main, Apr  6 2023, 07:55:46) [GCC 11.3.0]
- ujson        : 5.7.1.dev26
- orjson       : 3.9.0
- simplejson   : 3.19.1
- json         : 2.0.9

|                                                                               | ujson      | orjson     | simplejson | json       |
|-------------------------------------------------------------------------------|-----------:|-----------:|-----------:|-----------:|
| Array with 256 doubles                                                        |            |            |            |            |
| encode                                                                        |     18,282 |     79,569 |      5,681 |      5,935 |
| decode                                                                        |     28,765 |     93,283 |     13,844 |     13,367 |
| Array with 256 UTF-8 strings                                                  |            |            |            |            |
| encode                                                                        |      3,457 |     26,437 |      3,630 |      3,653 |
| decode                                                                        |      3,576 |      4,236 |        522 |      1,978 |
| Array with 256 strings                                                        |            |            |            |            |
| encode                                                                        |     44,769 |    125,920 |     21,401 |     23,565 |
| decode                                                                        |     28,518 |     75,043 |     41,496 |     42,221 |
| Medium complex object                                                         |            |            |            |            |
| encode                                                                        |     11,672 |     47,659 |      3,913 |      5,729 |
| decode                                                                        |     12,522 |     23,599 |      8,007 |      9,720 |
| Array with 256 True values                                                    |            |            |            |            |
| encode                                                                        |    110,444 |    425,919 |     81,428 |     84,347 |
| decode                                                                        |    203,430 |    318,193 |    146,867 |    156,249 |
| Array with 256 dict{string, int} pairs                                        |            |            |            |            |
| encode                                                                        |     14,170 |     72,514 |      3,050 |      7,079 |
| decode                                                                        |     19,116 |     27,542 |      9,374 |     13,713 |
| Dict with 256 arrays with 256 dict{string, int} pairs                         |            |            |            |            |
| encode                                                                        |         55 |        282 |         11 |         26 |
| decode                                                                        |         48 |         53 |         27 |         34 |
| Dict with 256 arrays with 256 dict{string, int} pairs, outputting sorted keys |            |            |            |            |
| encode                                                                        |         42 |            |          8 |         27 |
| Complex object                                                                |            |            |            |            |
| encode                                                                        |        462 |            |        397 |        444 |
| decode                                                                        |        480 |        618 |        177 |        310 |

Above metrics are in call/sec, larger is better.

## Multi-threading/Free-threading support

UltraJSON has no global state so using `ujson` functions between threads is
organically thread safe and will produce consistent results with or without
Python's global interpreter lock. What is is not supported nor safe is mutating
an object in one thread whilst it's simultaneously being serialised by `ujson`
in another. In such usage, all bugs are features.

## Build options

For those with particular needs, such as Linux distribution packagers, several
build options are provided in the form of environment variables.

## HPy build

This repository carries a usable `ujson_hpy` module which ports the UltraJSON
binding layer to [HPy](https://hpyproject.org/). It is intentionally kept
separate from the default `ujson` extension so the classic package behavior
stays unchanged while the HPy surface evolves.

The HPy flow is opt-in:

```sh
UJSON_BUILD_HPY=1 python setup.py --hpy-abi=cpython --hpy-use-static-libs build_ext
```

When `UJSON_BUILD_HPY=1` is set, the build now defaults to producing only the
`ujson_hpy` extension. Set `UJSON_BUILD_CPYTHON_EXT=1` if you also
want to build the classic `ujson` extension in the same invocation.

For reproducible local builds, use the helper scripts in [`scripts/`](./scripts):

```sh
./scripts/hpy-build.sh cpython
./scripts/hpy-build.sh universal

./scripts/hpy-test.sh cpython
./scripts/hpy-test.sh universal
```

By default these write artifacts under `build/hpy-cpython/` and
`build/hpy-universal/`, and they default to HPy-only builds.

Current `ujson_hpy` status:

- `loads`, `decode`, and `load` support `str`, `bytes`, `bytearray`,
  C-contiguous bytes-like objects such as `memoryview` and `array('B', ...)`,
  and file-like `read()`.
- `dumps`, `encode`, and `dump` support the main UltraJSON encoder path,
  including `sort_keys`, `indent`, `allow_nan`, `reject_bytes`, `default`,
  `toDict`, `__json__`, and file-like `write()`.
- Universal ABI builds include a sibling loader stub next to
  `ujson_hpy.hpy0.so`, so the output directory is directly importable without
  `pkg_resources`.
- The full local validation set currently covers the classic suite, HPy
  CPython/Universal suites, HPy debug mode, and isolated smoke imports.

The module is suitable for host integration and performance work, but it is not
yet published as a drop-in replacement package.

For the exported API, universal build artifacts, and non-CPython host
integration guidance, see [`docs/ujson-hpy-api.md`](./docs/ujson-hpy-api.md).
For the current benchmark matrix and optimization roadmap, see
[`docs/hpy-performance-plan.md`](./docs/hpy-performance-plan.md).
For the measured HPy host `GetItem_i` optimization slice and upstreamable
patches, see [`docs/hpy-getitem-i-fastpath.md`](./docs/hpy-getitem-i-fastpath.md).

### External artifact validation

The following commands build the HPy artifact and exercise it through an
isolated import path, which is closer to how a non-CPython host would consume
the universal build:

```sh
./scripts/hpy-smoke.sh universal
./scripts/hpy-smoke.sh cpython
```

Verified locally on July 13, 2026:

- `ujson_hpy external smoke check passed` with `abi=universal`
- `ujson_hpy external smoke check passed` with `abi=cpython`

### External host examples

Import the built universal artifact directly from CPython:

```sh
./scripts/hpy-build.sh universal
PYTHONPATH=build/hpy-universal/lib .venv/bin/python -c 'import ujson_hpy; print(ujson_hpy.loads("{\"ok\":true,\"items\":[1,2]}"))'
```

Generic HPy-host flow:

```text
artifact = hpy.load_module("build/hpy-universal/lib/ujson_hpy.hpy0.so")
module = hpy.init_module("ujson_hpy", artifact)
loads = module.get_attr("loads")
dumps = module.get_attr("dumps")
value = loads("{\"answer\":42}")
text = dumps(value)
```

Nox-style wrapper sketch:

```text
module json {
    let _ujson = hpy.import_module("ujson_hpy", "build/hpy-universal/lib/ujson_hpy.hpy0.so")

    fn loads(text: str) -> Dynamic {
        return _ujson.loads(text)
    }

    fn dumps(value: Dynamic) -> str {
        return _ujson.dumps(value)
    }
}
```

### Local HPy benchmark snapshot

The helper below benchmarks the built artifact against `json` and, when
available in the local environment, classic `ujson`:

```sh
./scripts/hpy-benchmark-matrix.sh universal --repeat 9 --min-time 0.10
```

Local run on July 13, 2026, on `macOS-26.5.2-arm64-arm-64bit-Mach-O` with
`CPython 3.14.2` and the universal ABI. Results are median operations per
second from deterministically shuffled library order:

| benchmark | json | ujson_hpy | ujson |
| --- | --- | --- | --- |
| loads small object | 1,454,134 ops/s | 2,596,240 ops/s | 2,935,266 ops/s |
| loads medium object | 782,517 ops/s | 1,132,439 ops/s | 1,315,380 ops/s |
| loads comprehensive fixture | 44,463 ops/s | 96,165 ops/s | 111,130 ops/s |
| loads 1024-int array | 31,030 ops/s | 64,748 ops/s | 72,254 ops/s |
| dumps small object | 797,634 ops/s | 1,768,421 ops/s | 3,067,591 ops/s |
| dumps medium object | 452,018 ops/s | 686,742 ops/s | 1,136,511 ops/s |
| dumps comprehensive fixture | 61,185 ops/s | 64,080 ops/s | 95,135 ops/s |
| dumps 1024-int array | 54,728 ops/s | 43,868 ops/s | 64,818 ops/s |

These figures are machine-specific, but they provide a reproducible baseline
for this repository. The full cpython-vs-universal matrix and the optimization
roadmap live in [`docs/hpy-performance-plan.md`](./docs/hpy-performance-plan.md).
The validated HPy host `GetItem_i` fast-path experiment, which brings the
Universal 1024-int encode case to within about 3% of classic `ujson`, is
documented in
[`docs/hpy-getitem-i-fastpath.md`](./docs/hpy-getitem-i-fastpath.md).

## Contributing

Contributor workflow, test commands, and git hygiene guidance live in
[`CONTRIBUTING.md`](./CONTRIBUTING.md).

### Debugging symbols

#### UJSON_BUILD_NO_STRIP

By default, debugging symbols are stripped on Linux platforms. Setting this
environment variable with a value of `1` or `True` disables this behavior.

### Using an external or system copy of the double-conversion library

These two environment variables are typically used together, something like:

```sh
export UJSON_BUILD_DC_INCLUDES='/usr/include/double-conversion'
export UJSON_BUILD_DC_LIBS='-ldouble-conversion'
```

Users planning to link against an external shared library should be aware of
the ABI-compatibility requirements this introduces when upgrading system
libraries or copying compiled wheels to other machines.

#### UJSON_BUILD_DC_INCLUDES

One or more directories, delimited by `os.pathsep` (same as the `PATH`
environment variable), in which to look for `double-conversion` header files;
the default is to use the bundled copy.

#### UJSON_BUILD_DC_LIBS

Compiler flags needed to link the `double-conversion` library; the default
is to use the bundled copy.
