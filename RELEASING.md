# Release Checklist

This repository is an HPy-focused fork, so a release should prove both the
classic `ujson` path and the `ujson_hpy` path are in a known-good state.

## Before tagging

- [ ] Confirm `README.md`, `CONTRIBUTING.md`, and the HPy docs reflect the
      actual ABI support and benchmark story in the branch being released.
- [ ] Review `git status` and verify no local-only artifacts are staged.
- [ ] Run the classic suite:

```bash
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

- [ ] If the release includes performance claims, archive fresh matrix output:

```bash
./scripts/hpy-benchmark-matrix.sh cpython --repeat 9 --min-time 0.10
./scripts/hpy-benchmark-matrix.sh universal --repeat 9 --min-time 0.10
```

## Packaging decisions

- [ ] Confirm the release notes state whether the artifact is:
      - source-only repository release
      - private/internal package
      - public package with a name distinct from upstream `ujson`
- [ ] Keep the `ujson_hpy` module name explicit unless the compatibility
      contract has been intentionally widened.
- [ ] If host-runtime patch files are relevant to the release, mention the
      validated patch set in the notes.

## Tag and publish

- [ ] Create the release commit and tag.
- [ ] Push the branch and tag to `origin`.
- [ ] Publish the GitHub release in
      [mburakmmm/hpy-ujson](https://github.com/mburakmmm/hpy-ujson/releases).
- [ ] If a package was published, verify installation/import with the chosen
      distribution name and with a direct local artifact import of
      `ujson_hpy`.
