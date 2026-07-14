# Contributing

Thanks for contributing to `hpy-ujson`.

This repository carries an HPy port of UltraJSON's binding layer. The main goal
is to keep the existing C encoder/decoder core behavior stable while making the
module usable from HPy hosts, especially the universal ABI.

## Project Scope

Good contributions for this repository include:

- HPy compatibility and universal ABI fixes
- behavior parity improvements against `ujson`
- tests for encode/decode edge cases
- build, packaging, and documentation improvements
- host-integration documentation for non-CPython HPy runtimes

Changes that need extra care:

- modifications inside `src/ujson/lib/`
- behavior changes that intentionally diverge from upstream `ujson`
- large refactors without new tests

When changing behavior, include a regression test in the same patch.

## Development Setup

Create a local virtual environment and install the development dependencies:

```sh
python -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip setuptools setuptools-scm pytest "hpy>=0.9,<0.10" tox
```

The default package build installs the HPy CPython ABI module:

```sh
python -m pip install .
python -c 'import ujson_hpy; print(ujson_hpy._hpy_abi())'
```

The classic upstream extension remains available for regression testing:

```sh
UJSON_BUILD_HPY=0 UJSON_BUILD_CPYTHON_EXT=1 python setup.py build_ext --inplace
python -m pytest -q
```

The HPy port can be built with:

```sh
./scripts/hpy-build.sh cpython
./scripts/hpy-build.sh universal
```

The HPy-focused test suites can be run with:

```sh
./scripts/hpy-test.sh cpython
./scripts/hpy-test.sh universal
HPY=debug ./scripts/hpy-test.sh universal
```

Or through `tox`:

```sh
tox -e hpy-cpython
tox -e hpy-universal
tox -e lint
```

For performance work, use the benchmark matrix helper instead of ad-hoc loops:

```sh
./scripts/hpy-benchmark-matrix.sh cpython --repeat 9 --min-time 0.10
./scripts/hpy-benchmark-matrix.sh universal --repeat 9 --min-time 0.10
```

If you touch host-runtime-facing behavior, also run the isolated artifact smoke
checks:

```sh
./scripts/hpy-smoke.sh cpython
./scripts/hpy-smoke.sh universal
```

Native encoder and decoder smoke fuzzing can be run against a built artifact:

```sh
PYTHONPATH=build/hpy-universal/lib python tests/fuzz.py --module ujson_hpy --seed=0:1000
PYTHONPATH=build/hpy-universal/lib python tests/fuzz_decode.py --module ujson_hpy --seed=0:5000
```

Before changing packaging, verify the wheel and source distribution contract:

```sh
python -m pip install build twine
./scripts/check-package.sh
```

## Git Workflow

1. Create a topic branch from `main`.
2. Keep each branch focused on one logical change.
3. Commit in small reviewable slices.
4. Add or update tests before opening a pull request.
5. Rebase or merge `main` as needed to keep CI noise low.

Recommended branch naming examples:

- `docs/hpy-host-guide`
- `fix/universal-loader`
- `test/decoder-parity`

Recommended commit style:

- use an imperative subject line
- keep the subject under about 72 characters
- explain the user-visible reason in the body when the change is not obvious

Examples:

- `docs: add HPy host integration guide`
- `tests: cover float-backed enum encoding`
- `build: simplify universal stub loading`

## Pull Request Checklist

Before opening a pull request, please verify:

- the branch contains only files relevant to the change
- generated build outputs are not committed
- local virtualenv and editor files are not committed
- behavior changes include tests
- HPy changes were checked in both `cpython` and `universal` modes when relevant
- HPy runtime or performance changes include benchmark notes when the claim is
  performance-sensitive
- user-facing documentation was updated when the API or workflow changed

If the change affects the HPy surface, include the exact commands you ran in the
pull request description.

## Repository Hygiene

Please do not commit:

- `build/`
- virtual environments such as `.venv/`
- local editor configuration
- benchmark scratch files
- personal notes or runtime-specific experiments unrelated to this repository
- local HPy source trees used only for patch validation

The repository `.gitignore` covers the usual local artifacts, but please check
`git status` before committing.

## Design Notes

The HPy port deliberately keeps the JSON core in place and replaces the binding
layer around it. That means small, test-backed changes are preferred over broad
architectural rewrites.

Useful references:

- [README.md](./README.md)
- [docs/hpy-port-plan.md](./docs/hpy-port-plan.md)
- [docs/ujson-hpy-api.md](./docs/ujson-hpy-api.md)
- [docs/hpy-performance-plan.md](./docs/hpy-performance-plan.md)
- [docs/hpy-getitem-i-fastpath.md](./docs/hpy-getitem-i-fastpath.md)
- [Security policy](./.github/SECURITY.md)

## Reporting Issues

When filing a bug, please include:

- platform and compiler details
- Python version
- HPy ABI used: `cpython`, `hybrid`, or `universal`
- a minimal reproducer
- expected behavior and actual behavior

For crashes, memory issues, or ABI mismatches, a reduced reproducer is much more
useful than a large application trace.
