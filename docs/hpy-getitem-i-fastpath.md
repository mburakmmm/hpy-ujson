# HPy `GetItem_i` Host Fast Path

## Scope

This experiment changes the CPython implementation of the HPy Universal host,
not the `ujson_hpy` extension ABI. The same `ujson_hpy.hpy0.so` artifact is
loaded against baseline and patched host runtimes.

The installed HPy 0.9.0 implementation boxes every index and performs generic
mapping dispatch:

```c
PyObject *key = PyLong_FromSsize_t(idx);
HPy result = _py2h(PyObject_GetItem(_h2py(obj), key));
Py_DECREF(key);
```

The patch in
[`patches/hpy-0.9.0-getitem-i-fastpath.patch`](../patches/hpy-0.9.0-getitem-i-fastpath.patch)
adds these paths in order:

1. exact list access with explicit negative-index normalization and an owned
   result reference
2. exact tuple access with the same ownership and bounds behavior
3. `PySequence_GetItem` for other CPython sequences
4. the original boxed-key mapping fallback for non-sequences

Exact checks are important. List and tuple subclasses must continue to observe
their overridden `__getitem__` behavior. The exact-list macro path is disabled
when `Py_GIL_DISABLED` is defined because obtaining a borrowed list item and
then incrementing its reference is not safe under concurrent list mutation;
those builds retain the owned-reference `PySequence_GetItem` path.

## Relationship To HPy Main

HPy `master` already contains the third step, `PySequence_Check` followed by
`PySequence_GetItem`. The local HPy 0.9.0 package predates that optimization.
The upstream-facing part of this experiment is therefore the exact-list and
exact-tuple refinement before the existing sequence path, plus the additional
semantic regression test. That minimal patch is available as
[`patches/hpy-main-getitem-i-exact-fastpath.patch`](../patches/hpy-main-getitem-i-exact-fastpath.patch)
and was applied and tested against HPy `master` commit `b57a33c`.

## Reproduction

Apply the backport to an HPy 0.9.0 source tree and build the host runtime into
an isolated directory:

```sh
patch -p1 < /path/to/ujson-hpy/patches/hpy-0.9.0-getitem-i-fastpath.patch
python setup.py build_ext --build-lib /tmp/hpy-fastpath/lib \
  --build-temp /tmp/hpy-fastpath/temp
```

Run the focused HPy tests from that source tree:

```sh
python -m pytest -q test/test_object.py -k getitem_i
```

Run the affected `ujson_hpy` benchmarks with the patched host directory first
on `PYTHONPATH`:

```sh
PYTHONPATH=/tmp/hpy-fastpath/lib:build/hpy-universal/lib \
  .venv/bin/python scripts/hpy_benchmark_matrix.py \
  --repeat 11 --min-time 0.15 --show-spread \
  --filter 'dumps medium object' \
  --filter 'dumps comprehensive fixture' \
  --filter 'dumps 1024-int array' \
  --filter 'dumps 128-record object'
```

## Results

Local A/B results on July 13, 2026, using CPython 3.14.2 on Apple Silicon:

| Universal encode case | HPy 0.9.0 | sequence path | exact path | exact vs 0.9.0 |
| --- | ---: | ---: | ---: | ---: |
| medium object | 679,038 | 701,043 | 711,967 | +4.9% |
| medium object `sort_keys` | 475,617 | 492,523 | 507,684 | +6.7% |
| comprehensive fixture | 62,990 | 65,947 | 65,706 | +4.3% |
| 1024-int array | 43,880 | 60,663 | 62,650 | +42.8% |
| 128-record object | 10,171 | 10,671 | 10,800 | +6.2% |

For the 1024-int array, the exact path reached `62,650 ops/s` while classic
`ujson` measured `64,458 ops/s`, leaving a 2.8% gap on this machine. The exact
path was 3.3% faster than the sequence path on the same workload.

## Validation

- HPy focused tests: `6 passed` across CPython, Universal, and debug modes
- HPy `master` object API suite: `78 passed` across the same ABI modes
- `ujson_hpy` full suite with the patched host: `654 passed, 1 xfailed`
- `ujson_hpy` HPy debug suite with the patched host: `178 passed`

This is a host-runtime optimization. Nox can obtain the same benefit by making
its `HPy_GetItem_i` implementation recognize native exact list and tuple
objects, normalize negative indices, and return an owned HPy handle without
constructing a language-level integer key.
