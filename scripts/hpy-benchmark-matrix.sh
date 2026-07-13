#!/usr/bin/env bash
set -euo pipefail

mode="${1:-universal}"
shift || true

case "${mode}" in
  cpython|universal)
    ;;
  *)
    echo "usage: $0 [cpython|universal] [benchmark args...]" >&2
    exit 2
    ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
build_root="${HPY_BUILD_ROOT:-${repo_root}/build/hpy-${mode}}"
build_lib="${HPY_BUILD_LIB:-${build_root}/lib}"
python_bin="${PYTHON:-${repo_root}/.venv/bin/python}"

UJSON_BUILD_CPYTHON_EXT=1 "${script_dir}/hpy-build.sh" "${mode}"

echo "Running HPy benchmark matrix"
echo "  mode: ${mode}"
echo "  python: ${python_bin}"
echo "  PYTHONPATH: ${build_lib}"

PYTHONPATH="${build_lib}" "${python_bin}" "${script_dir}/hpy_benchmark_matrix.py" "$@"
