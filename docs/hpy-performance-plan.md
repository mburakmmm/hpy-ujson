# HPy Performance Plan

## Goal

Use measured data to improve `ujson_hpy` without trading away correctness or HPy
host portability.

This document is intentionally driven by local benchmark results from this
repository, not by guesswork.

## Benchmark Method

Date:

- 2026-07-13

Machine:

- `macOS-26.5.2-arm64-arm-64bit-Mach-O`

Python:

- `CPython 3.14.2`

Commands used:

```sh
bash scripts/hpy-benchmark-matrix.sh cpython --repeat 9 --min-time 0.10
bash scripts/hpy-benchmark-matrix.sh universal --repeat 9 --min-time 0.10
```

These commands build:

- `ujson_hpy` in the requested HPy ABI mode
- classic `ujson` in the same build output directory

Then they benchmark:

- stdlib `json`
- `ujson_hpy`
- classic `ujson`

using the same process, same Python interpreter, and same payload set. The
runner calibrates each callback, disables GC during each sample, shuffles the
library order deterministically for every round, and reports median throughput.
This avoids the fixed-order and optimistic-minimum bias in the first version of
the benchmark runner.

## Current Validated Results

These are the accepted post-optimization results. The span is omitted here for
readability; use `--show-spread` to include it in local output.

### HPy CPython ABI

| benchmark | json | ujson_hpy | ujson |
| --- | --- | --- | --- |
| dumps small object | 796,481 ops/s | 2,175,539 ops/s | 3,047,302 ops/s |
| dumps medium object | 454,770 ops/s | 922,260 ops/s | 1,119,224 ops/s |
| dumps comprehensive fixture | 60,914 ops/s | 85,429 ops/s | 93,455 ops/s |
| dumps 1024-int array | 54,842 ops/s | 71,127 ops/s | 64,761 ops/s |
| dumps 128-record object | 10,236 ops/s | 13,474 ops/s | 14,520 ops/s |
| loads comprehensive fixture | 44,658 ops/s | 106,275 ops/s | 111,231 ops/s |
| loads 1024-int array | 30,663 ops/s | 68,881 ops/s | 70,651 ops/s |

### HPy Universal ABI

| benchmark | json | ujson_hpy | ujson |
| --- | --- | --- | --- |
| dumps small object | 797,634 ops/s | 1,768,421 ops/s | 3,067,591 ops/s |
| dumps medium object | 452,018 ops/s | 686,742 ops/s | 1,136,511 ops/s |
| dumps comprehensive fixture | 61,185 ops/s | 64,080 ops/s | 95,135 ops/s |
| dumps 1024-int array | 54,728 ops/s | 43,868 ops/s | 64,818 ops/s |
| dumps 128-record object | 10,074 ops/s | 10,452 ops/s | 14,663 ops/s |
| loads comprehensive fixture | 44,463 ops/s | 96,165 ops/s | 111,130 ops/s |
| loads 1024-int array | 31,030 ops/s | 64,748 ops/s | 72,254 ops/s |

The latest encode slice produced these important outcomes:

- CPython ABI `dumps 1024-int array` now exceeds classic `ujson` on this host.
- CPython ABI is within about 7-9% of classic `ujson` on the comprehensive and
  record-heavy dumps.
- Universal `dumps 1024-int array` improved from the prior stable result of
  about `34,568` to `43,868 ops/s`, roughly a 27% gain.
- Universal decode remains within roughly 10-16% of classic `ujson` on the
  larger measured payloads.

## Historical Results

The tables below use the original fixed-order/minimum-result runner. They are
kept to preserve the optimization history, but current comparisons should use
the validated tables above.

### HPy CPython ABI

| benchmark | json | ujson_hpy | ujson |
| --- | --- | --- | --- |
| loads small object | 1,467,394 ops/s | 2,683,943 ops/s | 2,989,370 ops/s |
| dumps small object | 793,637 ops/s | 2,032,321 ops/s | 3,058,279 ops/s |
| loads medium object | 794,284 ops/s | 1,218,542 ops/s | 1,341,099 ops/s |
| dumps medium object | 460,427 ops/s | 882,834 ops/s | 1,135,852 ops/s |
| dumps medium object sort_keys | 403,152 ops/s | 556,837 ops/s | 814,273 ops/s |
| loads comprehensive fixture | 44,067 ops/s | 104,926 ops/s | 112,173 ops/s |
| loads comprehensive bytes | 42,296 ops/s | 106,414 ops/s | 113,836 ops/s |
| loads comprehensive bytearray | 42,122 ops/s | 105,673 ops/s | 114,256 ops/s |
| dumps comprehensive fixture | 60,622 ops/s | 83,782 ops/s | 93,485 ops/s |
| loads 1024-int array | 31,743 ops/s | 65,248 ops/s | 69,831 ops/s |
| dumps 1024-int array | 51,631 ops/s | 62,514 ops/s | 61,704 ops/s |
| loads 128-record object | 11,639 ops/s | 15,406 ops/s | 15,854 ops/s |
| dumps 128-record object | 9,579 ops/s | 12,713 ops/s | 14,222 ops/s |

### HPy Universal ABI

| benchmark | json | ujson_hpy | ujson |
| --- | --- | --- | --- |
| loads small object | 1,471,150 ops/s | 2,475,635 ops/s | 2,922,774 ops/s |
| dumps small object | 805,169 ops/s | 1,660,998 ops/s | 2,998,123 ops/s |
| loads medium object | 773,658 ops/s | 1,110,516 ops/s | 1,277,991 ops/s |
| dumps medium object | 462,487 ops/s | 632,630 ops/s | 1,159,770 ops/s |
| dumps medium object sort_keys | 405,291 ops/s | 454,058 ops/s | 799,179 ops/s |
| loads comprehensive fixture | 44,013 ops/s | 95,662 ops/s | 110,958 ops/s |
| loads comprehensive bytes | 41,411 ops/s | 95,448 ops/s | 112,105 ops/s |
| loads comprehensive bytearray | 41,568 ops/s | 94,555 ops/s | 111,152 ops/s |
| dumps comprehensive fixture | 59,255 ops/s | 57,821 ops/s | 92,667 ops/s |
| loads 1024-int array | 30,865 ops/s | 61,079 ops/s | 68,309 ops/s |
| dumps 1024-int array | 52,040 ops/s | 36,118 ops/s | 62,396 ops/s |
| loads 128-record object | 11,273 ops/s | 13,594 ops/s | 15,729 ops/s |
| dumps 128-record object | 9,667 ops/s | 9,590 ops/s | 14,686 ops/s |

## Phase 2 Progress Snapshot

After the first Phase 2 optimization slice, the encode path improved
substantially on the same local benchmark matrix.

HPy cpython ABI improvements relative to the original baseline:

- `dumps small object`: `668,833 -> 1,604,786 ops/s` (`+140%`)
- `dumps medium object`: `202,128 -> 592,933 ops/s` (`+193%`)
- `dumps medium object sort_keys`: `185,321 -> 430,991 ops/s` (`+133%`)
- `dumps comprehensive fixture`: `19,656 -> 52,505 ops/s` (`+167%`)
- `dumps 1024-int array`: `17,052 -> 26,069 ops/s` (`+53%`)
- `dumps 128-record object`: `2,595 -> 8,502 ops/s` (`+228%`)

HPy universal ABI improvements relative to the original baseline:

- `dumps small object`: `650,861 -> 1,403,952 ops/s` (`+116%`)
- `dumps medium object`: `193,439 -> 509,478 ops/s` (`+163%`)
- `dumps medium object sort_keys`: `178,263 -> 378,816 ops/s` (`+113%`)
- `dumps comprehensive fixture`: `18,124 -> 47,382 ops/s` (`+161%`)
- `dumps 1024-int array`: `16,446 -> 24,362 ops/s` (`+48%`)
- `dumps 128-record object`: `2,435 -> 7,452 ops/s` (`+206%`)

The main Phase 2 target of getting `dumps medium object` above `300,000 ops/s`
has now been reached in both HPy ABI modes.
The `cpython` HPy build now also beats stdlib `json` on:

- `dumps small object`
- `dumps medium object`
- `dumps medium object sort_keys`

The universal HPy build now beats stdlib `json` on:

- `dumps small object`
- `dumps medium object`

## Phase 4 Progress Snapshot

After pooling encoder type-context allocations and removing repeated empty-tuple
method-call overhead on the encode path, `ujson_hpy` gained another substantial
step on the same local benchmark matrix.

HPy cpython ABI improvements relative to the previous encode baseline:

- `dumps small object`: `1,586,703 -> 1,819,119 ops/s` (`+15%`)
- `dumps medium object`: `587,037 -> 735,166 ops/s` (`+25%`)
- `dumps medium object sort_keys`: `428,965 -> 508,294 ops/s` (`+18%`)
- `dumps comprehensive fixture`: `54,305 -> 69,647 ops/s` (`+28%`)
- `dumps 1024-int array`: `25,509 -> 40,986 ops/s` (`+61%`)
- `dumps 128-record object`: `8,478 -> 10,719 ops/s` (`+26%`)

HPy universal ABI improvements relative to the previous encode baseline:

- `dumps small object`: `1,469,275 -> 1,692,815 ops/s` (`+15%`)
- `dumps medium object`: `519,068 -> 643,206 ops/s` (`+24%`)
- `dumps medium object sort_keys`: `395,339 -> 459,441 ops/s` (`+16%`)
- `dumps comprehensive fixture`: `46,252 -> 58,021 ops/s` (`+25%`)
- `dumps 1024-int array`: `23,716 -> 36,403 ops/s` (`+53%`)
- `dumps 128-record object`: `7,356 -> 9,073 ops/s` (`+23%`)

The result is that `ujson_hpy` now beats stdlib `json` on these encode
benchmarks in both HPy ABIs:

- `dumps small object`
- `dumps medium object`
- `dumps medium object sort_keys`
- `dumps comprehensive fixture`

## Phase 5 Progress Snapshot

After adding CPython-ABI direct iteration for lists, tuples, and unsorted
dictionaries, array-heavy encode workloads improved sharply again without
changing the universal path.

HPy cpython ABI improvements relative to the previous encode baseline:

- `dumps small object`: `1,819,119 -> 2,032,321 ops/s` (`+12%`)
- `dumps medium object`: `735,166 -> 882,834 ops/s` (`+20%`)
- `dumps medium object sort_keys`: `508,294 -> 556,837 ops/s` (`+10%`)
- `dumps comprehensive fixture`: `69,647 -> 83,782 ops/s` (`+20%`)
- `dumps 1024-int array`: `40,986 -> 62,514 ops/s` (`+52%`)
- `dumps 128-record object`: `10,719 -> 12,713 ops/s` (`+19%`)

HPy universal ABI status relative to the same baseline:

- no equivalent universal fast path was added in this slice
- results stayed in the same general band, which confirms the latest gain is
  specifically from CPython borrowed-item iteration shortcuts

The result is that the HPy cpython ABI now also beats stdlib `json` on:

- `dumps 1024-int array`
- `dumps 128-record object`

A follow-up scalar type-hint cache experiment on per-item wrappers was measured
and then rejected. It increased hot-path work in iteration-heavy dumps and
regressed the key array case from about `57,031 -> 34,070 ops/s` in HPy
cpython ABI and from about `34,568 -> 18,934 ops/s` in HPy universal ABI on
the same local matrix, so that approach was reverted instead of being carried
forward.

A second follow-up experiment tried to inline list/tuple scalar items
(`bool`/`null`/`int`) during iteration so the encoder could skip handle
wrappers for those elements. That also lost against the current baseline:
`dumps 1024-int array` moved from about `57,031 -> 54,117 ops/s` in HPy
cpython ABI and from about `34,568 -> 33,152 ops/s` in HPy universal ABI.
The extra per-element classification work cost more than the wrapper reuse it
eliminated, so that design was also reverted.

## Phase 6 Progress Snapshot

System sampling of universal `dumps(list(range(1024)))` identified two
independent costs:

- `HPy_GetItem_i` boxes every index and performs generic `PyObject_GetItem`
  dispatch in HPy's CPython universal runtime.
- scalar contexts were closing several null handles and allocating pooled
  contexts even when no owned resources existed.

The accepted optimization slice now:

- skips `HPy_Close` calls for null handles
- uses borrowed list, tuple, and dict values in CPython ABI, matching the
  lifetime model used by classic `ujson`
- skips resource cleanup for contexts that never acquired a handle
- avoids allocating an encoder type-context for ordinary int, bool, null,
  float, bytes, and non-surrogate Unicode scalar values
- keeps big integers, default-converted values, raw JSON hooks, containers, and
  surrogate fallback on the owned-context path
- materializes custom separators as owned UTF-8 bytes so their buffers remain
  valid under HPy's debug handle runtime

A follow-up attempt borrowed raw UTF-8 pointers from dictionary keys to avoid
per-key byte materialization. Normal ABI tests passed, but HPy's debug runtime
crashed while encoding a basic dictionary because the pointer lifetime is not
portable across the debug handle boundary. The experiment was rejected and
dictionary names remain backed by owned `bytes` handles.

Validation gates passed:

- CPython ABI: `174 passed, 4 skipped`
- universal ABI: `178 passed`
- universal debug runtime (`HPY=debug`): `178 passed`
- external artifact smoke tests: passed for CPython and universal ABI

## Phase 7 Host Fast-Path Snapshot

System profiling showed that HPy 0.9.0's CPython Universal host boxes every
`HPy_GetItem_i` index and dispatches through `PyObject_GetItem`. A host-runtime
prototype now handles exact lists and tuples directly while preserving the
owned-handle contract, negative indices, bounds errors, and subclass behavior.

The serial 11-repeat A/B matrix measured these Universal encode changes:

| benchmark | HPy 0.9.0 host | sequence host path | exact host path |
| --- | ---: | ---: | ---: |
| dumps medium object | 679,038 ops/s | 701,043 ops/s | 711,967 ops/s |
| dumps medium object sort_keys | 475,617 ops/s | 492,523 ops/s | 507,684 ops/s |
| dumps comprehensive fixture | 62,990 ops/s | 65,947 ops/s | 65,706 ops/s |
| dumps 1024-int array | 43,880 ops/s | 60,663 ops/s | 62,650 ops/s |
| dumps 128-record object | 10,171 ops/s | 10,671 ops/s | 10,800 ops/s |

The exact path improved the 1024-int case by 42.8% over the installed HPy
0.9.0 host and by 3.3% over the generic sequence fast path. Classic `ujson`
measured 64,458 ops/s in the exact-path run, leaving a 2.8% gap.

HPy `master` already has the generic `PySequence_GetItem` improvement. The
remaining upstream contribution is the exact-list and exact-tuple refinement,
plus the semantic regression test. The local backport, commands, and full
rationale are in [`hpy-getitem-i-fastpath.md`](./hpy-getitem-i-fastpath.md).

Validation gates passed:

- HPy `GetItem_i` tests: `6 passed` across CPython, Universal, and debug
- HPy `master` object API suite: `78 passed`
- patched-host `ujson_hpy` full suite: `654 passed, 1 xfailed`
- patched-host `ujson_hpy` debug suite: `178 passed`

## Phase 3 Progress Snapshot

After introducing a decode-side wrapper pool for transient `HpyJsonValue`
objects, the large-object loads path improved sharply on the same local
benchmark matrix.

HPy cpython ABI improvements relative to the Phase 2 baseline:

- `loads small object`: `2,083,388 -> 2,602,036 ops/s` (`+25%`)
- `loads medium object`: `767,795 -> 1,193,634 ops/s` (`+55%`)
- `loads comprehensive fixture`: `60,324 -> 104,887 ops/s` (`+74%`)
- `loads comprehensive bytes`: `59,609 -> 106,322 ops/s` (`+78%`)
- `loads comprehensive bytearray`: new `104,864 ops/s`
- `loads 1024-int array`: `31,551 -> 64,056 ops/s` (`+103%`)
- `loads 128-record object`: `8,954 -> 14,922 ops/s` (`+67%`)

HPy universal ABI improvements relative to the Phase 2 baseline:

- `loads small object`: `2,070,003 -> 2,512,304 ops/s` (`+21%`)
- `loads medium object`: `740,035 -> 1,132,263 ops/s` (`+53%`)
- `loads comprehensive fixture`: `57,464 -> 93,772 ops/s` (`+63%`)
- `loads comprehensive bytes`: `57,972 -> 94,445 ops/s` (`+63%`)
- `loads comprehensive bytearray`: new `93,897 ops/s`
- `loads 1024-int array`: `33,876 -> 62,526 ops/s` (`+85%`)
- `loads 128-record object`: `8,679 -> 13,517 ops/s` (`+56%`)

The result is that `ujson_hpy` now beats stdlib `json` on every measured
decode benchmark in both HPy ABI modes.
The cpython ABI now also has a dedicated zero-copy fast path for contiguous
buffer inputs, which keeps `bytearray` loads essentially at the same level as
raw `bytes`.

## Key Findings

### 1. Universal ABI overhead is workload-dependent

Decode results are relatively close between the CPython and universal HPy
builds, so the universal loader model is not the primary decode bottleneck.
Encode is different: large containers expose a structural gap because the
current universal API has no iterator or borrowed list-item primitive.

After the latest cpython-only buffer fast path, the biggest remaining ABI gap
shows up exactly where expected: contiguous buffer inputs such as `bytearray`,
where cpython can use `PyObject_GetBuffer` directly and universal still has to
fall back to object-level adaptation. On encode, the biggest ABI gap is now
large list and dict traversal.

### 2. Decode is now faster than stdlib `json` across the full benchmark set

After the current Phase 3 work, `ujson_hpy` beats stdlib `json` on all measured
decode workloads in both ABIs, including:

- `loads medium object`
- `loads comprehensive fixture`
- `loads comprehensive bytes`
- `loads comprehensive bytearray`
- `loads 1024-int array`
- `loads 128-record object`

It still trails classic `ujson`, but on several decode cases the remaining gap
is now modest rather than structural.

### 3. Encode is now the dominant remaining bottleneck

Encode performance is now split clearly by ABI. After the current Phase 5 work,
the HPy cpython build is faster than stdlib `json` on every measured encode
benchmark in this matrix, while the universal build still trails on the
largest array-heavy cases.

Examples:

- comprehensive fixture dumps: about `1.40x` faster than stdlib in HPy cpython ABI
- 1024-int array dumps: about `30%` faster than stdlib in HPy cpython ABI
- 128-record object dumps: about `32%` faster than stdlib in HPy cpython ABI
- 1024-int array dumps: about `20%` slower than stdlib in HPy universal ABI

Against classic `ujson`, the Universal encode gap is much larger. The CPython
ABI is already close on nested payloads and ahead on the integer-array case.

For the universal runtime on CPython, one concrete reason is now verified in
the local HPy runtime sources: `ctx_GetItem_i(...)` boxes every index with
`PyLong_FromSsize_t(idx)` and then dispatches through `PyObject_GetItem(...)`
for each element access. That means large array dumps in universal ABI are not
limited only by `ujson_hpy` logic; they are also paying a per-element indexed
lookup cost in the host runtime layer.

### 4. Large nested decode still has headroom against classic `ujson`

The worst large decode regression against stdlib has been eliminated, but
`ujson_hpy` still trails classic `ujson` on the heavier object graphs.

Examples:

- `loads 128-record object`: about `7%` slower than classic `ujson` in HPy cpython ABI
- `loads 128-record object`: about `14%` slower than classic `ujson` in HPy universal ABI
- `loads comprehensive fixture`: about `5%` slower than classic `ujson` in HPy cpython ABI
- `loads comprehensive fixture`: about `16%` slower than classic `ujson` in HPy universal ABI
- `loads comprehensive bytearray`: about `6%` slower than classic `ujson` in HPy cpython ABI
- `loads comprehensive bytearray`: about `17%` slower than classic `ujson` in HPy universal ABI

## Likely Hotspots

The current results point to binding-layer overhead in these areas:

### Encode path

- [src/ujson/python/ujson_hpy.c](/Users/melihburakmemis/Documents/ujson-hpy/src/ujson/python/ujson_hpy.c:1728)
  `hpy_encoder_begin_type_context(...)`
- [src/ujson/python/ujson_hpy.c](/Users/melihburakmemis/Documents/ujson-hpy/src/ujson/python/ujson_hpy.c:2133)
  `hpy_encode_python_object(...)`
- [src/ujson/python/ujson_hpy.c](/Users/melihburakmemis/Documents/ujson-hpy/src/ujson/python/ujson_hpy.c:1557)
  `hpy_setup_dict_iter(...)`
- [src/ujson/python/ujson_hpy.c](/Users/melihburakmemis/Documents/ujson-hpy/src/ujson/python/ujson_hpy.c:1292)
  `hpy_tuple_iter_next(...)`
- [src/ujson/python/ujson_hpy.c](/Users/melihburakmemis/Documents/ujson-hpy/src/ujson/python/ujson_hpy.c:1404)
  `hpy_dict_iter_next(...)`
- [src/ujson/python/ujson_hpy.c](/Users/melihburakmemis/Documents/ujson-hpy/src/ujson/python/ujson_hpy.c:1025)
  `hpy_unicode_to_utf8_raw(...)`

Specific reasons:

- repeated HPy type checks in the main encode dispatch
- per-item owned handles in universal list, tuple, and dict iteration
- repeated Unicode and key conversion work on the encode boundary
- generic indexed lookup in the universal HPy runtime

After the current encode work, the remaining gap is concentrated in:

- per-element list/tuple iteration overhead on large homogeneous arrays
- repeated `HPy_GetItem_i` traffic for array-heavy dumps
- unicode conversion and key materialization on the remaining object cases

For the universal ABI specifically, the first two bullets now dominate much
more clearly than they do on cpython.

### Decode path

- [src/ujson/python/ujson_hpy.c](/Users/melihburakmemis/Documents/ujson-hpy/src/ujson/python/ujson_hpy.c:554)
  `hpy_loads_decode_raw(...)`
- [src/ujson/python/ujson_hpy.c](/Users/melihburakmemis/Documents/ujson-hpy/src/ujson/python/ujson_hpy.c:667)
  `hpy_loads_decode_buffer_like(...)`
- [src/ujson/python/ujson_hpy.c](/Users/melihburakmemis/Documents/ujson-hpy/src/ujson/python/ujson_hpy.c:759)
  `hpy_loads_dispatch(...)`

Specific reasons:

- buffer-like decode still materializes a `memoryview` and then copies with
  `tobytes()`
- string creation and Python container population still dominate the remaining
  decode gap more than wrapper allocation now does

For the HPy cpython ABI specifically:

- contiguous bytes-like inputs now bypass the fallback `memoryview(...).tobytes()`
  path and decode directly from a borrowed `Py_buffer`
- unicode string creation and container population are now the larger remaining
  decode costs

## Phase Plan

### Phase 1: Measurement Hardening

Purpose:

- keep a stable benchmark baseline while changing code

Tasks:

- keep `scripts/hpy-benchmark-matrix.sh` as the default local perf entrypoint
- record cpython-vs-universal results after every meaningful perf patch
- add a short benchmark summary section to PR descriptions for perf work

Acceptance gate:

- benchmark matrix runs cleanly for both HPy ABIs
- each performance change includes before/after numbers

### Phase 2: Encode Fast-Path Acceleration

Purpose:

- close the biggest current gap first

Primary targets:

- `hpy_encoder_begin_type_context`
- iterator setup and per-item object wrapping
- unicode and bytes conversion on the encode path

Tasks:

- remove heap allocation from the hot encode type-context path where feasible
- separate the fast path for common builtins from slow paths such as `toDict`,
  `__json__`, and `default`
- reduce repeated `HPy_HasAttr_s` and related hook probes for builtin container
  types
- reduce temporary wrapper churn for list, tuple, and dict iteration

Target outcomes:

- `dumps medium object` above `300,000 ops/s` in HPy cpython ABI
- `dumps comprehensive fixture` above `30,000 ops/s` in HPy cpython ABI
- universal ABI staying within about `10%` of the HPy cpython ABI result

Current status:

- both absolute throughput targets have been exceeded by a wide margin
- the original goal of keeping Universal within 10% of CPython is not met on
  array-heavy encode and now depends on a lower-overhead HPy traversal primitive
- the next encode gains are now most likely in array iteration and string/key
  conversion, not type-context allocation

Current status after the latest slice:

- cpython array iteration is no longer the primary encode blocker
- universal array iteration is now the clearest remaining encode hotspot

### Phase 3: Decode and Large-Object Optimization

Purpose:

- keep decode competitive while improving large nested structures

Primary targets:

- `hpy_loads_decode_buffer_like`
- object and array construction callbacks
- large nested object handle churn

Current status:

- transient decode wrapper churn has been reduced substantially with a pooled
  allocator
- the next universal-ABI decode gains are more likely to come from avoiding
  extra copies for buffer-like inputs and reducing string/container creation
  overhead

Tasks:

- avoid avoidable copies on buffer-like input paths where the HPy host surface
  permits it
- audit object and array creation callbacks for unnecessary temporary handles
- measure the large-object regression case after every decode-side change

Target outcomes:

- `loads 128-record object` at or above stdlib `json`
- `loads comprehensive fixture` kept above stdlib `json`

### Phase 4: Stabilization

Purpose:

- prevent performance fixes from breaking semantics or leaking handles

Tasks:

- rerun `tests/test_ujson_hpy.py`
- rerun `tests/test_ujson_hpy_upstream_subset.py`
- rerun the universal leak checks
- keep external artifact smoke validation in the loop:
  - `bash scripts/hpy-smoke.sh cpython`
  - `bash scripts/hpy-smoke.sh universal`

Acceptance gate:

- no behavior regressions
- no new handle leaks
- no benchmark regressions on previously improved cases

### Phase 5: Host-Facing Development

Purpose:

- make performance work useful to downstream HPy hosts

Tasks:

- keep README and API docs aligned with measured results
- document which payload classes are now decode-competitive
- once a real non-CPython host is available, rerun the smoke and benchmark
  surface there instead of only through CPython's HPy host

Acceptance gate:

- downstream hosts can reproduce the same artifact validation flow
- performance claims in docs match measured reality

## Immediate Work Order

The next changes should be taken in this order:

1. prepare the exact-list and exact-tuple refinement as an HPy `master` pull
   request and run it through HPy's supported CPython version matrix
2. benchmark the upstream host path on Linux and macOS; an additional iterator
   primitive is not justified unless those results regress materially
3. propose an HPy UTF-8 output construction API that can transfer or copy the
   encoder buffer without creating an intermediate `bytes` object
4. add a Universal buffer-view API path for decode so contiguous bytearray and
   memoryview inputs do not require `memoryview(...).tobytes()`
5. rerun the complete matrix and debug runtime after each accepted primitive
6. validate the artifact and benchmark surface on a real non-CPython HPy host;
   Nox should implement `HPy_GetItem_i` with the same container fast-path goal

The host prototype closed almost all of the Universal integer-array gap, so the
first item is now upstream stabilization rather than further local speculation.
After that, output-buffer construction is the next measured encode boundary.
