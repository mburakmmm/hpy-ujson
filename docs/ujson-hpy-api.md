# `ujson_hpy` API and Host Integration Guide

## Purpose

`ujson_hpy` is an HPy port of UltraJSON's Python binding layer. It keeps the
existing UltraJSON encoder/decoder core in C and exposes it as an HPy module
named `ujson_hpy`.

This document is written for two audiences:

- users importing `ujson_hpy` from CPython for testing or validation
- runtime authors loading `ujson_hpy` from a non-CPython HPy host such as a
  custom language runtime

## Build Outputs

Build the universal artifact with:

```sh
./scripts/hpy-build.sh universal
```

The default output directory is:

```text
build/hpy-universal/lib/
```

Universal builds produce:

- `ujson_hpy.hpy0.so`
- `ujson_hpy.py`

The `.py` file is a CPython convenience stub for HPy universal imports. A
non-CPython HPy host should load `ujson_hpy.hpy0.so` directly.

## Module Contract

The module exports:

- `encode(obj, ...)`
- `decode(obj)`
- `dumps(obj, ...)`
- `loads(obj)`
- `dump(obj, fp, ...)`
- `load(fp)`
- `JSONDecodeError`
- `__version__`
- `_hpy_abi()`

`encode` is an alias of `dumps`. `decode` is an alias of `loads`.

## API Reference

### `loads(obj) -> value`

Decode JSON from one of the following input types:

- text string
- bytes
- bytearray
- C-contiguous bytes-like objects such as `memoryview` or `array('B', ...)`

Returns decoded values using the host's object model:

- JSON `null` -> host `None`/null object
- JSON booleans -> host bool
- JSON numbers -> host int/float
- JSON strings -> host unicode string
- JSON arrays -> host list
- JSON objects -> host dict

Raises:

- `JSONDecodeError` for invalid JSON
- `TypeError` for unsupported input objects

### `decode(obj) -> value`

Alias of `loads(obj)`.

### `load(fp) -> value`

Calls `fp.read()` and decodes the result via `loads`.

`fp.read()` must return one of the accepted `loads` input types.

Raises:

- `TypeError("expected file")` if `fp` has no callable `read`
- exceptions raised by `fp.read()`
- `JSONDecodeError` or `TypeError` from the decode path

### `dumps(obj, *, ensure_ascii=True, encode_html_chars=False, escape_forward_slashes=True, sort_keys=False, indent=0, allow_nan=True, reject_bytes=True, default=None, separators=None) -> str`

Encode supported host objects to a JSON string.

Supported values include:

- `None`
- `bool`
- `int`
- `float`
- `str`
- `list`
- `tuple`
- `dict`
- `decimal.Decimal` when the host exposes a `decimal.Decimal` type
- `bytes` only when `reject_bytes=False`

Additional object conversion hooks:

- `default(obj)` callback
- `obj.toDict()`
- `obj.__json__()`

Options:

- `ensure_ascii`: escape non-ASCII characters when true
- `encode_html_chars`: escape `<`, `>`, `&`
- `escape_forward_slashes`: escape `/`
- `sort_keys`: sort dict keys before encoding
- `indent`: `0` for compact output, `-1` for UltraJSON's spaced single-line
  style, positive values for pretty-printing
- `allow_nan`: when false, `NaN` and infinities raise `OverflowError`
- `reject_bytes`: when true, `bytes` values raise `TypeError`
- `default`: callback used for unsupported objects
- `separators`: custom `(item_separator, key_separator)` pair

Behavior notes:

- `indent < -1` is clamped to `-1`
- `indent > 1000` raises `ValueError`
- arbitrary objects with only `__float__` are not treated as JSON numbers
- invalid UTF-8 in encoded bytes input raises an error

Raises:

- `TypeError` for unsupported values or invalid option types
- `OverflowError` when `allow_nan=False` and NaN/infinity is encountered
- `ValueError` for invalid indentation
- exceptions raised by `default`, `toDict`, or `__json__`

### `encode(obj, **kwargs) -> str`

Alias of `dumps(obj, **kwargs)`.

### `dump(obj, fp, **kwargs) -> None`

Encodes through the same path as `dumps`, then calls `fp.write(result)`.

Raises:

- `TypeError("expected file")` if `fp` has no callable `write`
- any exception raised by `fp.write(...)`
- any encode-side exception from `dumps`

### `JSONDecodeError`

Module-specific decode exception type. It is a subclass of `ValueError`.

### `__version__`

String version exported at module initialization.

### `_hpy_abi() -> str`

Returns the active HPy ABI string, typically one of:

- `cpython`
- `hybrid`
- `universal`

Useful when validating that a host loaded the intended artifact.

## Non-CPython Host Integration

If your runtime is an HPy host, the intended integration model is:

1. Build `ujson_hpy` in universal mode.
2. Load `ujson_hpy.hpy0.so` through your HPy universal loader.
3. Instantiate the module as `ujson_hpy`.
4. Resolve the exported functions from the module object.
5. Wrap those functions in your runtime's standard library surface.

For example, a host-level wrapper usually only needs:

- `loads`
- `dumps`
- `JSONDecodeError`

Then your language can expose a native-facing API such as:

```text
json.loads(text) -> value
json.dumps(value) -> text
```

### Example host flow

Generic host pseudocode:

```text
artifact = hpy.load_module("ujson_hpy.hpy0.so")
module = hpy.init_module("ujson_hpy", artifact)
loads = module.get_attr("loads")
dumps = module.get_attr("dumps")
value = loads("{\"answer\":42}")
text = dumps(value)
```

Nox-style wrapper sketch:

```text
module json {
    let _ujson = hpy.import_module("ujson_hpy", "/abs/path/ujson_hpy.hpy0.so")

    fn loads(text: str) -> Dynamic {
        return _ujson.loads(text)
    }

    fn dumps(value: Dynamic) -> str {
        return _ujson.dumps(value)
    }
}
```

## Host Requirements

To run this module outside CPython, the HPy host must support more than basic
scalar calls. `ujson_hpy` needs:

- module loading and `HPy_mod_exec`
- keyword-argument calls
- attribute lookup and method calls
- unicode and bytes objects
- list, tuple, and dict objects
- error creation and propagation
- import support used during module init

In practice the required surface includes these HPy families:

- module and defs:
  - `HPyModuleDef`
  - `HPy_MODINIT`
  - `HPy_mod_exec`
  - `HPyDef_METH`
- argument parsing:
  - `HPyArg_ParseKeywords`
  - `HPyFunc_KEYWORDS`
  - `HPyFunc_NOARGS`
- objects and calls:
  - `HPy_GetAttr_s`
  - `HPy_HasAttr_s`
  - `HPyCallable_Check`
  - `HPy_CallTupleDict`
  - `HPy_CallMethodTupleDict_s`
- strings and bytes:
  - `HPyUnicode_*`
  - `HPyBytes_*`
- containers:
  - `HPyTuple_*`
  - `HPyList_*`
  - `HPyDict_*`
- numbers and type checks:
  - `HPyLong_*`
  - `HPyFloat_*`
  - `HPy_Type`
  - `HPy_TypeCheck`
  - `HPyType_IsSubtype`
- exceptions and globals:
  - `HPyErr_*`
  - `HPyGlobal_Load`
  - `HPyGlobal_Store`
  - `HPyImport_ImportModule`

If a host only supports a minimal subset such as `HPyFunc_O` and basic scalar
types, that host is not yet sufficient for `ujson_hpy`.

## Host-Specific Notes

### Builtins and `decimal`

At module import time, `ujson_hpy` attempts to import:

- `builtins`
- `decimal`

This is used to cache:

- `bytearray`
- `memoryview`
- `decimal.Decimal`

If those imports are unavailable in your host, module import still succeeds, but
the related fast paths or type checks may be reduced:

- `bytearray` support may be unavailable
- `memoryview` support may be unavailable
- `decimal.Decimal` support may be unavailable

### File-like objects

`load` and `dump` are protocol-based, not path-based:

- `load` expects an object with callable `read()`
- `dump` expects an object with callable `write(value)`

If your host does not model Python-style file objects, expose a small adapter
object that provides those methods.

### Python compatibility hooks

`dumps` looks for:

- `toDict`
- `__json__`

If your runtime wants those features, your host objects must expose them as
attributes callable from HPy.

## Recommended First Integration Slice

For a new runtime, do not start with the entire surface.

Start with:

1. universal module loading
2. `loads(str)` and `dumps(dict/list/str/int/bool/null)`
3. exception propagation

Then add:

- bytes-like decode input
- `load` and `dump`
- custom `default`
- `toDict` and `__json__`
- `sort_keys`, `separators`, and indentation parity

## Current Status

This port is intentionally named `ujson_hpy` and remains separate from the
classic `ujson` module. It is suitable for integration work and host validation,
but it is still documented in this repository as experimental rather than a
published drop-in replacement package.
