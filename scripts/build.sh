#!/usr/bin/env bash

set -euo pipefail

preset="${1:-linux-gcc-debug}"

case "${preset}" in
  linux-gcc-debug|linux-gcc-release|linux-clang-analysis)
    ;;
  *)
    echo "Unsupported preset: ${preset}" >&2
    exit 2
    ;;
esac

cmake --workflow --preset "${preset}"
