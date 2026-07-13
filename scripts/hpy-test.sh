#!/usr/bin/env bash
set -euo pipefail

mode="${1:-cpython}"
shift || true

case "${mode}" in
  cpython|universal)
    ;;
  *)
    echo "usage: $0 [cpython|universal] [pytest args...]" >&2
    exit 2
    ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
build_root="${HPY_BUILD_ROOT:-${repo_root}/build/hpy-${mode}}"
build_lib="${HPY_BUILD_LIB:-${build_root}/lib}"
python_bin="${PYTHON:-${repo_root}/.venv/bin/python}"

"${script_dir}/hpy-build.sh" "${mode}"

cd "${repo_root}"

if [[ "$#" -eq 0 ]]; then
  set -- tests/test_ujson_hpy.py tests/test_ujson_hpy_upstream_subset.py
fi

echo "Running HPy tests"
echo "  mode: ${mode}"
echo "  python: ${python_bin}"
echo "  PYTHONPATH: ${build_lib}"

PYTHONPATH="${build_lib}" "${python_bin}" -m pytest -q "$@"
