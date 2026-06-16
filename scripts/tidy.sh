#!/usr/bin/env bash
# Lint ctfsm with clang-tidy (Google style + correctness/perf).
# Single source of truth for the linted file set; used by devs, the CMake `tidy`
# target, and CI.
#
# ctfsm is header-only, so clang-tidy lints the example/test translation units
# that include the headers; the headers themselves surface through them (bounded
# by HeaderFilterRegex in .clang-tidy). It therefore needs a configured build
# with compile_commands.json (CMAKE_EXPORT_COMPILE_COMMANDS is ON by default).
# Point at the build dir with BUILD_DIR (default: build).
#
# Uses the pinned clang-tidy (requirements-dev.txt). Override the binary with the
# CLANG_TIDY env var (e.g. a venv: CLANG_TIDY=.venv/bin/clang-tidy).
#
#   scripts/tidy.sh            # lint; non-zero exit on any finding (CI gate)
#   scripts/tidy.sh --fix      # apply clang-tidy's safe fixes in place
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

CT="${CLANG_TIDY:-clang-tidy}"
BUILD_DIR="${BUILD_DIR:-build}"
command -v "$CT" >/dev/null 2>&1 || {
  echo "error: clang-tidy not found ('$CT'). Install: pip install -r requirements-dev.txt" >&2
  exit 127
}
[ -f "$BUILD_DIR/compile_commands.json" ] || {
  echo "error: $BUILD_DIR/compile_commands.json missing. Configure the build first:" >&2
  echo "       cmake -S . -B $BUILD_DIR -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
  exit 2
}

# clang-tidy parses with its OWN bundled clang, not the compiler that built the
# compile DB. On a box with several GCCs, that clang may pick a GCC dir holding
# only runtime libs (not the C++ headers) and die with "cstddef file not found".
# Pin it to the same libstdc++ the project's GCC uses (derived, so it stays
# portable across machines/CI).
extra=()
ref_cxx="${CXX:-g++}"
if command -v "$ref_cxx" >/dev/null 2>&1 \
   && gcc_lib="$("$ref_cxx" -print-libgcc-file-name 2>/dev/null)" \
   && [ -n "$gcc_lib" ] && [ -d "$(dirname "$gcc_lib")" ]; then
  extra+=(--extra-arg-before="--gcc-install-dir=$(dirname "$gcc_lib")")
fi

# Lint exactly the TUs the configured build compiled — from the compile DB, not a
# raw find — so each file gets its real flags and the build/ tree (fetched
# googletest, the compile-fail negative tests) is excluded automatically.
mapfile -t files < <(python3 - "$BUILD_DIR/compile_commands.json" "$PWD/" <<'PY'
import json, os, sys
db, root = sys.argv[1], sys.argv[2]
build = os.path.join(root, "build")
seen = set()
for e in json.load(open(db)):
    f = e["file"]
    if f.startswith(root) and not f.startswith(build) \
            and f.endswith((".cpp", ".cc", ".cxx")) and f not in seen:
        seen.add(f)
        print(f)
PY
)
[ ${#files[@]} -gt 0 ] || { echo "no C++ TUs found in $BUILD_DIR/compile_commands.json"; exit 0; }

fix=()
[ "${1:-}" = "--fix" ] && fix=(--fix --fix-errors)

ver="$("$CT" --version | sed -n 's/.*LLVM version //p' | tr -d ' ')"
echo "clang-tidy ${ver} on ${#files[@]} TUs (-p ${BUILD_DIR})"
printf '%s\0' "${files[@]}" \
  | xargs -0 -P "$(nproc)" -I{} "$CT" -p "$BUILD_DIR" --quiet "${extra[@]}" "${fix[@]}" {}
echo "tidy OK (${#files[@]} TUs, clang-tidy ${ver})"
