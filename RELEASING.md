# Release Checklist

This repository publishes the `hpy-ujson` distribution and imports it as
`ujson_hpy`. A release should prove both the primary HPy path and the retained
classic `ujson` regression path are in a known-good state.

## Before tagging

- [ ] Confirm `README.md`, `CONTRIBUTING.md`, and the HPy docs reflect the
      actual ABI support and benchmark story in the branch being released.
- [ ] Review `git status` and verify no local-only artifacts are staged.
- [ ] Run the classic suite:

```bash
UJSON_BUILD_HPY=0 UJSON_BUILD_CPYTHON_EXT=1 .venv/bin/python setup.py build_ext --inplace
.venv/bin/python -m pytest -q
```

- [ ] Run the HPy suites:

```bash
./scripts/hpy-test.sh cpython
./scripts/hpy-test.sh universal
HPY=debug ./scripts/hpy-test.sh universal
```

- [ ] Run external artifact smoke checks:

```bash
./scripts/hpy-smoke.sh cpython
./scripts/hpy-smoke.sh universal
```

- [ ] Build and inspect the wheel and source distribution:

```bash
.venv/bin/python -m pip install build twine
./scripts/check-package.sh
```

- [ ] Run deterministic native-path smoke fuzzing:

```bash
PYTHONPATH=build/hpy-universal/lib .venv/bin/python tests/fuzz.py --module ujson_hpy --seed=0:1000
PYTHONPATH=build/hpy-universal/lib .venv/bin/python tests/fuzz_decode.py --module ujson_hpy --seed=0:5000
```

- [ ] If the release includes performance claims, archive fresh matrix output:

```bash
./scripts/hpy-benchmark-matrix.sh cpython --repeat 9 --min-time 0.10
./scripts/hpy-benchmark-matrix.sh universal --repeat 9 --min-time 0.10
```

## Packaging contract

- [ ] Confirm the distribution metadata name is `hpy-ujson`.
- [ ] Confirm the installed import name is `ujson_hpy`.
- [ ] Confirm the package does not install or register classic `ujson`.
- [ ] Keep the `ujson_hpy` module name explicit unless the compatibility
      contract has been intentionally widened.
- [ ] If host-runtime patch files are relevant to the release, mention the
      validated patch set in the notes.

## Tag and publish

Automated PyPI publishing is intentionally disabled. Do not add a publishing
workflow until trusted publishing is configured specifically for `hpy-ujson`
and the wheel policy for Universal ABI hosts is documented.

- [ ] Create the release commit and tag.
- [ ] Push the branch and tag to `origin`.
- [ ] Publish the GitHub release in
      [mburakmmm/hpy-ujson](https://github.com/mburakmmm/hpy-ujson/releases).
- [ ] If a package was published, verify `pip install hpy-ujson` in a clean
      environment and perform a direct Universal ABI artifact import.
