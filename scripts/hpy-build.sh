#!/usr/bin/env bash
set -euo pipefail

mode="${1:-cpython}"
shift || true

case "${mode}" in
  cpython|universal)
    ;;
  *)
    echo "usage: $0 [cpython|universal] [extra setup.py args...]" >&2
    exit 2
    ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
python_bin="${PYTHON:-${repo_root}/.venv/bin/python}"
build_root="${HPY_BUILD_ROOT:-${repo_root}/build/hpy-${mode}}"
build_temp="${HPY_BUILD_TEMP:-${build_root}/temp}"
build_lib="${HPY_BUILD_LIB:-${build_root}/lib}"

mkdir -p "${build_temp}" "${build_lib}"

cd "${repo_root}"

cmd=(
  "${python_bin}"
  setup.py
  --hpy-use-static-libs
)

if [[ "${mode}" == "universal" ]]; then
  cmd+=(--hpy-abi=universal)
else
  cmd+=(--hpy-abi=cpython)
fi

cmd+=(
  build_ext
  --build-temp "${build_temp}"
  --build-lib "${build_lib}"
)

if [[ "$#" -gt 0 ]]; then
  cmd+=("$@")
fi

echo "Building HPy artifact"
echo "  mode: ${mode}"
echo "  python: ${python_bin}"
echo "  build temp: ${build_temp}"
echo "  build lib: ${build_lib}"
echo "  build classic ujson: ${UJSON_BUILD_CPYTHON_EXT:-0}"

UJSON_BUILD_HPY=1 UJSON_BUILD_CPYTHON_EXT="${UJSON_BUILD_CPYTHON_EXT:-0}" "${cmd[@]}"

echo "Artifacts written to ${build_lib}"
