# UltraJSON HPy Port Plan

## Objective

Port UltraJSON's Python extension layer from the CPython C-API to HPy without
changing the JSON encoder/decoder core behavior. The end-state should support:

- HPy universal ABI as the primary target.
- CPython ABI builds during migration for easier debugging.
- The existing `ujson` Python-level API and test behavior.
- Reuse in a non-CPython HPy-capable runtime.

## Current Code Layout

The repository is already split in a useful way:

- `src/ujson/lib/ultrajsonenc.c`
- `src/ujson/lib/ultrajsondec.c`
- `src/ujson/lib/ultrajson.h`

These files are Python-agnostic core code and communicate through callbacks.

The CPython-specific boundary is concentrated in three files:

- `src/ujson/python/ujson.c`
- `src/ujson/python/JSONtoObj.c`
- `src/ujson/python/objToJSON.c`

That split is the main reason this port is feasible without rewriting the JSON
engine itself.

## What Makes This Port Non-Trivial

The hard part is not the module definition macros. The hard part is object
identity and lifetime across the callback boundary.

`ultrajson.h` defines:

```c
typedef void * JSOBJ;
```

The current binding passes `PyObject *` through that `void *` channel. HPy
universal mode cannot do that because `HPy` is an opaque handle, not a raw
object pointer. A direct text replacement from `PyObject *` to `HPy` would
compile in some places but would be architecturally wrong.

This means the port needs a bridge layer that owns handles explicitly instead of
smuggling object pointers through `JSOBJ`.

## Recommended Port Strategy

Use an adapter-first port. Keep the encoder/decoder core intact. Replace only
the Python binding layer.

### Core rule

Do not teach `ultrajsonenc.c` or `ultrajsondec.c` about HPy directly unless we
discover a real blocker. The core already has the right abstraction: callbacks.
We should exploit it.

### Bridge design

Introduce a small HPy object wrapper owned by the binding layer, for example:

```c
typedef struct {
  HPy handle;
  int owns_handle;
} HpyJsonObj;
```

`JSOBJ` will carry `HpyJsonObj *`, not raw `HPy`.

This gives us three things:

1. A stable `void *` payload for the existing core API.
2. Explicit handle ownership.
3. A place to cache temporary data needed by iterator/type-context callbacks.

## Migration Phases

### Phase 0: Baseline and Environment

Goal: establish a reproducible build and test loop before changing behavior.

Tasks:

- Create a local virtual environment.
- Install build/test dependencies:
  - `setuptools`
  - `setuptools-scm`
  - `pytest`
  - `hpy`
- Verify the unmodified upstream package builds and imports.
- Run the existing test suite and record the baseline.

Acceptance criteria:

- `python -m pytest` runs locally.
- We know whether any upstream tests already fail on the local toolchain.

### Phase 1: Build System HPy Bootstrap

Goal: add HPy-aware build plumbing without yet porting all code.

Tasks:

- Update `pyproject.toml` build requirements to include `hpy`.
- Replace the plain `ext_modules`-only setup with HPy-aware configuration.
- Add an HPy extension target that can be built in CPython ABI first, then
  universal ABI.
- Keep the existing extension build path available until parity is reached.

Acceptance criteria:

- A minimal HPy module can be built from this repository.
- The build can switch between CPython ABI and universal ABI.

Current repository state:

- HPy bootstrap build is opt-in via `UJSON_BUILD_HPY=1`.
- HPy builds now default to producing only `ujson_hpy`.
- Set `UJSON_BUILD_CPYTHON_EXT=1` to also build the classic `ujson` extension
  in the same invocation when a side-by-side comparison is useful.
- Verified command for the current workspace:
  - `UJSON_BUILD_HPY=1 .venv/bin/python setup.py --hpy-use-static-libs build_ext --build-temp /private/tmp/ujson-hpy-build/temp --build-lib /private/tmp/ujson-hpy-build/lib`
- Current bootstrap artifact is `ujson_hpy`, not a drop-in replacement for
  `ujson` yet.
- Current implemented surface:
  - module init
  - `__version__`
  - `JSONDecodeError`
  - `decode` / `loads` for Python `str`, `bytes`, `bytearray`, and
    C-contiguous bytes-like input via the existing UltraJSON decoder core
  - `load` for file-like objects exposing a callable `read()`, including
    bytes-like payloads such as `memoryview`
  - `encode` / `dumps` via the existing UltraJSON encoder core for:
    - scalars (`None`, `bool`, `int`, `float`, `decimal.Decimal`, `str`)
    - `bytes` when `reject_bytes=False`
    - `list`, `tuple`, and `dict`
    - `default=...`, `toDict()`, and `__json__()`
    - `sort_keys`, `indent`, `allow_nan`, and `separators`
  - `dump` for file-like objects exposing a callable `write()`
- Still not implemented:
  - full encoder parity audit against the upstream `ujson` test matrix

### Phase 2: Module Init and Long-Lived State

Goal: port `src/ujson/python/ujson.c`.

Tasks:

- Replace `PyMethodDef`/`PyModuleDef`/`PyInit_ujson` with:
  - `HPyDef_METH`
  - `HPyModuleDef`
  - `HPy_MODINIT`
- Move initialization work to `HPy_mod_exec`.
- Replace long-lived globals with HPy-safe storage:
  - `JSONDecodeError`
  - cached `decimal.Decimal` type
- Use `HPyGlobal` for process/module lifetime handles.

Acceptance criteria:

- The module imports.
- `ujson.__version__` exists.
- `ujson.JSONDecodeError` exists and behaves as a `ValueError` subclass.

### Phase 3: Decoder Port (`loads`, `load`)

Goal: port `src/ujson/python/JSONtoObj.c`.

Tasks:

- Rewrite argument handling for:
  - `loads`
  - `decode`
  - `load`
- Replace all object constructors with HPy equivalents.
- Replace dict/list insertion and error handling with HPy equivalents.
- Port buffer/unicode input handling.
- Use the wrapper bridge for values returned through decoder callbacks.
- Preserve exact error messages where tests depend on them.

Key hotspots:

- bytes-like input path
- unicode to UTF-8 conversion
- `JSONDecodeError` raising
- file-like `read()` wrapper

Acceptance criteria:

- All decode/load tests pass against the HPy build.
- Error classes and messages remain stable where asserted by tests.

Current repository state:

- HPy decode path now covers:
  - `loads(str)`
  - `loads(str)` with surrogate-containing input preserved via `surrogatepass`
  - `loads(bytes)`
  - `loads(bytearray)`
  - `loads(memoryview(...))` for C-contiguous views
  - `loads(array('B', ...))` via the buffer protocol
  - `decode(...)` through the same dispatcher
  - `load(fp)` when `fp.read()` returns `str`, `bytes`, `bytearray`, or a
    supported bytes-like object
- Verified HPy CPython ABI test command:
  - `PYTHONPATH=/private/tmp/ujson-hpy-build/lib .venv/bin/python -m pytest -q tests/test_ujson_hpy.py`
  - current result: `34 passed, 3 skipped`

### Phase 4: Encoder Port (`dumps`, `dump`)

Goal: port `src/ujson/python/objToJSON.c`.

Tasks:

- Rewrite argument handling for:
  - `dumps`
  - `encode`
  - `dump`
- Port type dispatch:
  - bool
  - int / big int fallback
  - float / decimal
  - str
  - bytes
  - dict
  - list / tuple
  - custom `toDict`
  - custom `__json__`
  - `default=...`
- Replace iteration helpers with HPy-safe wrappers.
- Port all temporary object lifetime logic explicitly.

Key hotspots:

- `sort_keys` path
- `separators` validation
- `default` callback recursion path
- `__json__` and `toDict` dynamic dispatch
- refcount-sensitive code that currently relies on borrowed references

Acceptance criteria:

- All encode/dump tests pass against the HPy build.
- No leaked handles under HPy debug mode.

Current repository state:

- HPy encoder path is no longer a stub:
  - `PYTHONPATH=/private/tmp/ujson-hpy-build/lib .venv/bin/python -m pytest -q tests/test_ujson_hpy.py`
  - current result: `34 passed, 3 skipped`
- The main upstream suite still passes in the default non-HPy path:
  - `.venv/bin/python -m pytest -q`
  - current result: `476 passed, 38 skipped, 1 xfailed`
- Remaining work in this phase is now parity hardening rather than initial
  plumbing.
- Added a separate upstream-derived HPy parity subset:
  - `tests/test_ujson_hpy_upstream_subset.py`
  - verified with CPython ABI artifact:
    - `PYTHONPATH=/private/tmp/ujson-hpy-build/lib .venv/bin/python -m pytest -q tests/test_ujson_hpy.py tests/test_ujson_hpy_upstream_subset.py`
    - current result: `174 passed, 4 skipped`
  - verified with universal ABI artifact:
    - `PYTHONPATH=/private/tmp/ujson-hpy-universal/lib .venv/bin/python -m pytest -q tests/test_ujson_hpy.py tests/test_ujson_hpy_upstream_subset.py`
    - current result: `178 passed`

### Phase 5: HPy Cleanup and Universal ABI Hardening

Goal: remove migration shortcuts and prove the port is runtime-portable.

Tasks:

- Eliminate remaining direct `Python.h` dependencies from the binding layer.
- Run HPy debug mode if available.
- Verify universal ABI build artifact imports and passes tests.
- Audit for accidental reliance on borrowed references or pointer identity.

Acceptance criteria:

- Binding layer compiles against HPy without hidden CPython-only assumptions.
- Universal ABI is the default validated target.

Current repository state:

- Universal ABI stub loading no longer depends on `pkg_resources`.
- HPy helper scripts now default to HPy-only output instead of also building
  the classic `ujson` extension.
- Verified universal build command:
  - `UJSON_BUILD_HPY=1 .venv/bin/python setup.py --hpy-abi=universal --hpy-use-static-libs build_ext --build-temp /private/tmp/ujson-hpy-universal/temp --build-lib /private/tmp/ujson-hpy-universal/lib`
- Verified universal runtime command:
  - `PYTHONPATH=/private/tmp/ujson-hpy-universal/lib .venv/bin/python -m pytest -q tests/test_ujson_hpy.py`
  - current result: `37 passed`
- The custom `--build-lib` path now also receives a sibling `ujson_hpy.py`
  loader next to `ujson_hpy.hpy0.so`, so the universal artifact can be
  imported directly from the output directory.
- Universal-only leak checks now pass via `hpy.debug.LeakDetector` for decode,
  encode, and dump flows.
- The decode path now accepts C-contiguous buffer exporters without relying on
  direct `Python.h` buffer APIs, rejects non-contiguous `memoryview` objects
  with a `TypeError`, and now preserves surrogate-containing `str` input during
  decode via the same `surrogatepass` normalization used elsewhere.
- The upstream-derived HPy parity subset now also includes refcount-sensitive
  checks for decode buffers, dict keys/values, and exception paths, which pass
  under both CPython ABI and universal ABI builds in this workspace.
- The same parity subset now also covers additional encoder behavior around
  `default=` fallthrough and `separators` output/round-trip handling, including
  unusual separator strings and empty-collection behavior, with matching
  results under both validated HPy ABIs.
- `sort_keys` parity now also covers the ordered happy path, mixed-type key
  failures, and recursive `default=` refcount cleanup, again with matching
  results under both validated HPy ABIs.
- Hook dispatch parity now also covers `toDict` precedence over `__json__`,
  complex raw-JSON `__json__` payloads, and exception propagation from both
  hook types, including nested unserializable values returned from `toDict`.
- Decode parity now also covers nested malformed JSON raising
  `JSONDecodeError`, the array depth limit boundary, and the comprehensive JSON
  fixture roundtrip plus ujson-specific permissive cases such as `01`, `1.`,
  and bare `NaN` / `Infinity` tokens.
- Selected decode error-message parity now also covers malformed string inputs
  and representative invalid UTF-8 cases, including unterminated escapes,
  malformed unicode escapes, truncated continuation bytes, overlong encodings,
  and code points beyond `U+10FFFF`.
- A Phase 5 audit also found and fixed a real encoder parity bug: `float`
  subclasses such as `float`-backed `Enum` members now serialize through the
  double path like upstream `ujson`, without widening support to arbitrary
  objects that merely implement `__float__`.
- The latest closing audit did not uncover any additional source-level parity
  regressions in the remaining small upstream cases that were probed; it added
  coverage for version-string shape, closed file-like `load` / `dump` errors,
  and 32-bit sign-bit boundary decode cases.

### Phase 6: Upstreaming or Forking Strategy

Goal: decide where the result lives.

Tasks:

- Split no-behavior-change refactors into separate commits.
- Evaluate whether upstream is a realistic target for the full port.
- If not, maintain a clean fork focused on HPy compatibility.

Acceptance criteria:

- We have a contribution path that does not block the technical work.

Current repository state:

- The HPy binding is no longer a bootstrap spike; in this workspace it passes:
  - CPython HPy ABI: `174 passed, 4 skipped`
  - universal HPy ABI: `178 passed`
- The upstream project still explicitly describes itself as maintenance-only in
  `README.md`, which materially lowers the odds of a large binding rewrite
  being accepted as a single upstream contribution even when technically sound.
- The HPy module is still exposed as `ujson_hpy`, not as an in-place
  replacement for `ujson`, which keeps the risk envelope manageable for both
  local experimentation and eventual packaging.

Recommended Phase 6 outcome:

1. Treat a clean HPy-focused fork as the primary delivery vehicle.
2. Treat upstream contribution as a secondary, opportunistic track.
3. Keep the fork import surface explicit (`ujson_hpy`) until a full packaging
   decision and compatibility contract are published.

Why this is the pragmatic path:

- It does not block your HPy-capable language/runtime work on upstream review
  latency or upstream policy constraints.
- It preserves the ability to ship HPy universal ABI artifacts on your own
  cadence.
- It keeps the diff reviewable and bisectable if upstream is willing to accept
  limited pieces later.

Concrete contribution strategy:

Track A: fork-first shipping path

- Keep this repository as the authoritative HPy implementation branch.
- Publish a separate distribution name later rather than overriding upstream
  `ujson` packaging semantics immediately.
- Preserve `ujson_hpy` as the module name until the release contract for a
  drop-in replacement is explicit.

Track B: upstreamable slices

- Only propose narrowly scoped patches that have a realistic chance of being
  accepted under a maintenance-only policy.
- Good candidates:
  - opt-in HPy build plumbing that leaves classic `ujson` behavior unchanged
  - universal loader fixes that remove avoidable runtime friction
  - no-behavior-change refactors that reduce binding complexity
  - test additions that pin existing behavior without changing default package
    output
- Do not assume the full `ujson_hpy` binding layer should be proposed in one
  upstream patch set.

Suggested commit series for publication:

1. Build-system and helper-script plumbing for opt-in HPy builds.
2. Universal loader/stub handling and import-path fixes.
3. Decode path port plus HPy-specific decode tests.
4. Encode path port plus HPy-specific encode tests.
5. Phase 5 parity and hardening fixes, including leak coverage and regression
   tests.
6. Documentation updates describing the experimental HPy module and its support
   envelope.

Patch queue for the first three publication slices:

Patch 1: build plumbing only

- Primary files:
  - `setup.py`
  - `pyproject.toml`
  - `tox.ini`
  - `scripts/hpy-build.sh`
  - `scripts/hpy-test.sh`
- Intended content:
  - opt-in `UJSON_BUILD_HPY` plumbing
  - HPy extension target registration
  - helper test/build commands
  - no default-package behavior change for classic `ujson`
- Acceptance gate:
  - default non-HPy test path still passes
  - HPy CPython ABI artifact builds successfully
- Recommended commit title:
  - `build: add opt-in HPy extension plumbing`
- Recommended commit body:
  - add HPy build requirements and opt-in extension registration
  - add helper build/test entry points for CPython and universal HPy ABIs
  - keep classic `ujson` as the default non-HPy build product
- `setup.py` scope note:
  - include:
    - `env_flag(...)`
    - `build_hpy` / `build_cpython_ext` gating
    - `hpy_ext_modules` registration for `ujson_hpy`
    - conditional `setup_requires=["hpy"]`
  - exclude for Patch 2:
    - `_write_stub_without_pkg_resources(...)`
    - sibling stub-loader generation logic
- Verification commands:
  - `.venv/bin/python -m pytest -q`
  - `UJSON_BUILD_HPY=1 .venv/bin/python setup.py --hpy-abi=cpython --hpy-use-static-libs build_ext`

Patch 2: universal loader and import ergonomics

- Primary files:
  - `setup.py`
  - `README.md`
- Intended content:
  - universal stub-loader override without `pkg_resources`
  - sibling loader generation for direct import from `build-lib`
  - user-facing build instructions for CPython/universal ABI helper flow
- Acceptance gate:
  - universal build artifact imports from output directory
  - no regression in CPython ABI helper build
- Recommended commit title:
  - `build: simplify HPy universal stub loading`
- Recommended commit body:
  - stop depending on `pkg_resources` for universal HPy loader stubs
  - emit a sibling loader next to the built universal artifact
  - document the direct-import helper flow for HPy builds
- `setup.py` scope note:
  - this patch owns `_write_stub_without_pkg_resources(...)`
  - this patch owns sibling stub emission via `get_ext_fullpath(...)`
- Verification commands:
  - `bash scripts/hpy-build.sh universal`
  - `PYTHONPATH=build/hpy-universal/lib .venv/bin/python -c "import ujson_hpy; print(ujson_hpy._hpy_abi())"`

Patch 3: decode path port and decode-focused tests

- Primary files:
  - `src/ujson/python/ujson_hpy.c`
  - `tests/test_ujson_hpy.py`
  - `tests/test_ujson_hpy_upstream_subset.py`
- Intended content:
  - HPy `loads` / `decode` / `load` path
  - bytes-like and file-like input handling
  - surrogate and decode-error regression coverage
- Acceptance gate:
  - HPy CPython ABI and universal ABI decode tests pass
  - no leaked handles in decode-focused universal leak checks
- Recommended commit title:
  - `hpy: port ujson decode and load paths`
- Recommended commit body:
  - implement HPy decode/load support for str, bytes-like, and file-like input
  - preserve decode error class behavior and surrogate handling
  - add HPy-specific decode parity tests for upstream behavior
- Verification commands:
  - `bash scripts/hpy-test.sh cpython tests/test_ujson_hpy.py tests/test_ujson_hpy_upstream_subset.py -k "load or loads or decode"`
  - `bash scripts/hpy-test.sh universal tests/test_ujson_hpy.py tests/test_ujson_hpy_upstream_subset.py -k "load or loads or decode"`

Recommended file allocation for later slices:

- Patch 4, encoder port:
  - `src/ujson/python/ujson_hpy.c`
  - `tests/test_ujson_hpy.py`
  - `tests/test_ujson_hpy_upstream_subset.py`
- Patch 5, parity hardening:
  - `src/ujson/python/ujson_hpy.c`
  - `tests/test_ujson_hpy.py`
  - `tests/test_ujson_hpy_upstream_subset.py`
  - `docs/hpy-port-plan.md`
- Patch 6, docs/fork-release framing:
  - `README.md`
  - `docs/hpy-port-plan.md`

Release gating checklist for the fork track:

- Keep the classic upstream test path green in the default non-HPy build.
- Keep HPy CPython ABI and universal ABI test commands green.
- Keep HPy debug leak checks green where available.
- Decide the eventual distribution name before publishing artifacts.
- Document supported import names, ABI targets, and non-goals clearly.

First fork-release checklist:

- Choose an initial package name distinct from upstream `ujson`.
- Decide whether the first published artifact ships only `ujson_hpy` or also a
  compatibility shim package.
- Freeze the supported ABI matrix for the first release:
  - CPython HPy ABI
  - universal HPy ABI
- Publish exact local verification commands in the release notes:
  - `.venv/bin/python -m pytest -q`
  - `bash scripts/hpy-test.sh cpython`
  - `bash scripts/hpy-test.sh universal`
- State explicit non-goals for v1:
  - no in-place replacement of upstream `ujson`
  - no promise yet of identical wheel naming/layout to upstream
  - no optimization work beyond parity and safety fixes

Decision to lock now:

- Primary path: fork-first, HPy-focused distribution.
- Secondary path: upstream only the smallest slices that are reviewable and do
  not require upstream to bless the entire experimental module at once.

## Suggested File-Level Refactor Plan

Before the full HPy rewrite, do small structural refactors with zero behavior
change:

1. Extract file-like helper logic from `load` and `dump`.
2. Isolate encoder option parsing from the encoder execution path.
3. Isolate decoder input normalization from `JSON_DecodeObject`.
4. Introduce a binding-local wrapper header for object helpers.

These refactors reduce review risk and make the HPy diff smaller.

## Test Surface

Current suite:

- `tests/test_ujson.py` contains 133 test functions.

This suite already covers most of the risky behaviors we must preserve:

- big integers
- decimals
- invalid UTF-8 and surrogate handling
- file-like objects
- separators
- sorting
- recursive/default callback cases
- bytes rejection behavior
- memory-sensitive paths

## Local Baseline Snapshot

Recorded on this workspace before HPy code changes:

- Python: `3.14.2`
- Local venv created at `.venv`
- Editable install works with:
  - `.venv/bin/python -m pip install -e . --no-build-isolation`
- Test result:
  - `476 passed, 1 xfailed`
- With opt-in HPy bootstrap enabled:
  - `PYTHONPATH=/private/tmp/ujson-hpy-build/lib .venv/bin/python -m pytest -q tests/test_ujson_hpy.py`
  - result: `8 passed`
- Universal bootstrap status:
  - `setup.py --hpy-abi=universal` produced `ujson_hpy.hpy0.so`
  - compile/link still succeeds after introducing the real `loads(str)` path
  - in this workspace the generated HPy stub loader still expects
    `pkg_resources`, which is not importable from the current venv
  - treat universal runtime loading as an open bootstrap issue, even though the
    universal extension artifact now compiles

Notes:

- Plain `pip install -e .` attempted build isolation and wanted network access
  for build dependencies even after they were installed in the venv.
- For this workspace, `--no-build-isolation` is the reproducible baseline path.

## Risks

### 1. `JSOBJ` transport model

This is the main architectural risk. If wrapper ownership is sloppy, the port
will leak handles or use closed handles.

Mitigation:

- Make ownership explicit in wrapper structs.
- Add narrow helper functions for create/dup/close/free.
- Run HPy debug mode before claiming parity.

### 2. Borrowed-reference assumptions

The current CPython code uses macros and APIs like `PyList_GET_ITEM`,
`PyTuple_GET_ITEM`, and `PyDict_GetItem`, which assume borrowed-reference
semantics. HPy pushes us toward explicit lifetime management.

Mitigation:

- Replace macro-style direct access with explicit helper calls.
- Audit every temporary object for who closes it.

### 3. Dynamic callback behavior

`default`, `toDict`, and `__json__` mix C recursion with Python calls and error
propagation.

Mitigation:

- Port these paths late, after simple scalar/container behavior is stable.
- Add focused regression tests if an existing test does not pin exact behavior.

### 4. Upstream acceptance

UltraJSON upstream currently describes the project as maintenance-only. A large
feature port may not be accepted even if technically sound.

Mitigation:

- Treat upstreaming as a separate track from technical delivery.
- Keep the fork clean and bisectable.

## Decisions To Lock Early

These should be fixed before implementation starts in earnest:

1. Primary target is HPy universal ABI, not only CPython ABI.
2. JSON core stays callback-based and mostly untouched.
3. `JSOBJ` in the binding layer carries wrapper pointers, not raw HPy handles.
4. Migration proceeds by decode path first, then encode path.
5. We maintain test parity before any optimization work.

## First Implementation Slice

The safest first coding slice is:

1. Set up venv and dependencies.
2. Add HPy build plumbing.
3. Port `ujson.c` module init only.
4. Port `loads` for `str` input only.
5. Extend `loads` to bytes-like and file-like inputs.
6. Port `dumps` for primitive scalars and strings.
7. Expand to containers and custom callbacks.

This order gives a fast executable checkpoint while keeping the risk contained.

## Definition of Done

The port is done only when all of the following are true:

- The HPy build passes the existing `ujson` test suite.
- The binding layer no longer relies on raw `PyObject *` transport.
- Universal ABI artifacts import successfully.
- HPy debug mode does not report leaked handles.
- Public Python behavior matches current `ujson` semantics closely enough for
  the existing tests and any added regression tests to pass.
