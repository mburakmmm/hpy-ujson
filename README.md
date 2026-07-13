# hpy-ujson

[![PyPI version](https://img.shields.io/pypi/v/ujson.svg?logo=pypi&logoColor=FFE873)](https://pypi.org/project/ujson)
[![Supported Python versions](https://img.shields.io/pypi/pyversions/ujson.svg?logo=python&logoColor=FFE873)](https://pypi.org/project/ujson)
[![PyPI downloads](https://img.shields.io/pypi/dm/ujson.svg)](https://pypistats.org/packages/ujson)
[![GitHub Actions status](https://github.com/ultrajson/ultrajson/workflows/Test/badge.svg)](https://github.com/ultrajson/ultrajson/actions)
[![codecov](https://codecov.io/gh/ultrajson/ultrajson/branch/main/graph/badge.svg)](https://codecov.io/gh/ultrajson/ultrajson)
[![DOI](https://zenodo.org/badge/1418941.svg)](https://zenodo.org/badge/latestdoi/1418941)
[![Code style: Black](https://img.shields.io/badge/code%20style-Black-000000.svg)](https://github.com/psf/black)

`hpy-ujson` is an HPy-oriented fork of UltraJSON: an ultra fast JSON encoder
and decoder written in pure C with bindings for Python and HPy hosts.

Install with pip:

```sh
python -m pip install ujson
```

## Project status

> [!WARNING]
> UltraJSON's architecture is fundamentally ill-suited to making changes without
> risk of introducing new security vulnerabilities. As a result, this library
> has been put into a *maintenance-only* mode. Support for new Python versions
> will be added and critical bugs and security issues will still be
> fixed but all other changes will be rejected. Users are encouraged to migrate
> to [orjson](https://pypi.org/project/orjson/) which is both much faster and
> less likely to introduce a surprise buffer overflow vulnerability in the
> future.

## Usage

May be used as a drop in replacement for most other JSON parsers for Python:

```pycon
>>> import ujson
>>> ujson.dumps([{"key": "value"}, 81, True])
'[{"key":"value"},81,true]'
>>> ujson.loads("""[{"key": "value"}, 81, true]""")
[{'key': 'value'}, 81, True]
```

### Encoder options

#### encode_html_chars

Used to enable special encoding of "unsafe" HTML characters into safer Unicode
sequences. Default is `False`:

```pycon
>>> ujson.dumps("<script>John&Doe", encode_html_chars=True)
'"\\u003cscript\\u003eJohn\\u0026Doe"'
```

#### ensure_ascii

Limits output to ASCII and escapes all extended characters above 127. Default is `True`.
If your end format supports UTF-8, setting this option to false is highly recommended to
save space:

```pycon
>>> ujson.dumps("åäö")
'"\\u00e5\\u00e4\\u00f6"'
>>> ujson.dumps("åäö", ensure_ascii=False)
'"åäö"'
```

#### escape_forward_slashes

Controls whether forward slashes (`/`) are escaped. Default is `True`:

```pycon
>>> ujson.dumps("https://example.com")
'"https:\\/\\/example.com"'
>>> ujson.dumps("https://example.com", escape_forward_slashes=False)
'"https://example.com"'
```

#### indent

Controls whether indentation ("pretty output") is enabled. Default is `0` (disabled):

```pycon
>>> ujson.dumps({"foo": "bar"})
'{"foo":"bar"}'
>>> print(ujson.dumps({"foo": "bar"}, indent=4))
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

## Experimental HPy build

This repository also carries an experimental `ujson_hpy` module which ports the
UltraJSON binding layer to [HPy](https://hpyproject.org/). It is currently kept
separate from the default `ujson` extension so the upstream package behavior
stays unchanged.

The HPy flow is opt-in:

```sh
UJSON_BUILD_HPY=1 python setup.py --hpy-abi=cpython --hpy-use-static-libs build_ext
```

When `UJSON_BUILD_HPY=1` is set, the build now defaults to producing only the
experimental `ujson_hpy` extension. Set `UJSON_BUILD_CPYTHON_EXT=1` if you also
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

This HPy module is still experimental and is not published as a drop-in
replacement package.

For the exported API, universal build artifacts, and non-CPython host
integration guidance, see [`docs/ujson-hpy-api.md`](./docs/ujson-hpy-api.md).

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
./scripts/hpy-benchmark.sh universal --loops 20000 --repeat 5
```

Local run on July 13, 2026, on `macOS-26.5.2-arm64-arm-64bit-Mach-O` with
`CPython 3.14.2` and the universal ABI:

| benchmark | json | ujson_hpy | ujson |
| --- | --- | --- | --- |
| dumps compact | 483,933 ops/s | 191,812 ops/s | 1,182,686 ops/s |
| loads compact | 777,200 ops/s | 654,594 ops/s | 1,330,842 ops/s |
| dumps sort_keys | 396,260 ops/s | 176,955 ops/s | 828,223 ops/s |

These figures are machine-specific, but they provide a reproducible baseline
for this repository and prove that the built universal artifact runs outside the
default in-tree import path.

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
