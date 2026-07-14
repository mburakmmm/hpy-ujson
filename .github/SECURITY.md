# Security Policy

## Current support status

`hpy-ujson` is currently alpha software intended for HPy runtime integration,
compatibility testing, and performance evaluation. It has not received an
independent security audit and is not yet recommended as a drop-in production
replacement for upstream `ujson`.

The repository runs compatibility tests, HPy debug-handle checks, isolated
artifact smoke tests, and deterministic encoder/decoder smoke fuzzing. These
checks reduce risk but are not a substitute for continuous coverage-guided
fuzzing or a security review.

## Reporting a vulnerability

Use [GitHub's private vulnerability reporting](https://github.com/mburakmmm/hpy-ujson/security)
when it is available. Do not publish exploit details in a public issue. If
private reporting is unavailable, open a public issue requesting a private
contact channel without including sensitive reproduction details.

Please include the affected commit or version, HPy ABI, host runtime, operating
system, and the smallest available reproducer. Reports involving memory safety,
untrusted JSON input, loader behavior, or handle lifetime are especially
important.
