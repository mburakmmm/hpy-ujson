#!/usr/bin/env bash
set -euo pipefail
shopt -s nullglob

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
python_bin="${PYTHON:-${repo_root}/.venv/bin/python}"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/hpy-ujson-package.XXXXXX")"
source_dir="${work_dir}/source"
package_version="$(cd "${repo_root}" && "${python_bin}" setup.py --version)"

cleanup() {
  rm -rf "${work_dir}"
}
trap cleanup EXIT

mkdir -p "${source_dir}"
rsync -a \
  --exclude '.DS_Store' \
  --exclude '.git/' \
  --exclude '.pytest_cache/' \
  --exclude '.venv/' \
  --exclude '*.egg-info/' \
  --exclude '*.so' \
  --exclude '__pycache__/' \
  --exclude 'build/' \
  --exclude 'dist/' \
  --exclude 'kendidilimicin/' \
  "${repo_root}/" "${source_dir}/"

cd "${source_dir}"
SETUPTOOLS_SCM_PRETEND_VERSION_FOR_HPY_UJSON="${package_version}" \
  SETUPTOOLS_SCM_PRETEND_VERSION="${package_version}" \
  "${python_bin}" -m build --sdist --wheel --outdir "${work_dir}/dist"
"${python_bin}" -m twine check --strict "${work_dir}"/dist/*

wheels=("${work_dir}"/dist/*.whl)
sdists=("${work_dir}"/dist/*.tar.gz)
if [[ "${#wheels[@]}" -ne 1 || "${#sdists[@]}" -ne 1 ]]; then
  echo "expected exactly one wheel and one source distribution" >&2
  exit 1
fi

"${python_bin}" -m venv "${work_dir}/venv"
"${work_dir}/venv/bin/python" -m pip install --no-deps "${wheels[0]}"
cd "${work_dir}"
"${work_dir}/venv/bin/python" - <<'PY'
from importlib import metadata, util

dist = metadata.distribution("hpy-ujson")
if dist.metadata["Name"] != "hpy-ujson":
    raise AssertionError(f"unexpected distribution name: {dist.metadata['Name']!r}")
python_requirements = set(dist.metadata["Requires-Python"].split(","))
if python_requirements != {">=3.10", "<3.15"}:
    raise AssertionError(
        f"unexpected Python requirement: {dist.metadata['Requires-Python']!r}"
    )

files = [str(item) for item in dist.files or ()]
if any(path.startswith("ujson-stubs/") for path in files):
    raise AssertionError("classic ujson stubs leaked into the hpy-ujson wheel")
if util.find_spec("ujson") is not None:
    raise AssertionError("the hpy-ujson wheel unexpectedly installs classic ujson")

import ujson_hpy

if ujson_hpy._hpy_abi() != "cpython":
    raise AssertionError(f"unexpected wheel ABI: {ujson_hpy._hpy_abi()!r}")
if ujson_hpy.loads(ujson_hpy.dumps({"package": "hpy-ujson"})) != {
    "package": "hpy-ujson"
}:
    raise AssertionError("installed wheel failed JSON roundtrip")

try:
    metadata.distribution("ujson")
except metadata.PackageNotFoundError:
    pass
else:
    raise AssertionError("the wheel unexpectedly registers an ujson distribution")

print("hpy-ujson distribution contract passed")
PY
