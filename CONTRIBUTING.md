# Contributing to ctfsm

Thanks for your interest. ctfsm is a small, focused library — contributions that keep it small, fast, and verifiable are very welcome.

## Build & test

```sh
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

Requires a C++17 compiler (GCC or Clang). GoogleTest is fetched automatically.

## Formatting & linting

Google style, **version-pinned** so everyone gets identical results. clang-format owns layout; clang-tidy owns what the formatter can't see — naming, correctness, performance, modernization (config + rationale in [`.clang-tidy`](.clang-tidy)). Both are CI gates.

```sh
python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt
find include test examples \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
  | xargs -0 .venv/bin/clang-format -i        # add --dry-run --Werror to check
CLANG_TIDY=.venv/bin/clang-tidy scripts/tidy.sh   # or: cmake --build build --target tidy
```

clang-tidy lints the example/test TUs the build compiled, so configure first (`cmake -S . -B build`). The trait/metafunction layer intentionally follows `std::` type-traits naming (snake_case `is_*`/`has_*`, `_v` suffixes, `Row::from`/`to` aliases) — that carve-out is documented in `.clang-tidy`.

Optional local git hooks catch problems before CI: run `scripts/hooks/install.sh` once per clone to point `core.hooksPath` at `scripts/hooks/` — **pre-commit** clang-formats staged files, **pre-push** runs the clang-tidy gate. Both use the pinned `.venv` tools and degrade to a notice if a tool or build is missing.

## Guidelines

- **Keep the hot path real-time.** `Update`/`Dispatch` and the lifecycle must stay allocation-free, `noexcept`, and free of RTTI/exceptions — the tests in `test/test_realtime.cpp` (0 allocations / 100k ticks) and the `-fno-exceptions -fno-rtti` build enforce this.
- **Test new behavior.** New public behavior needs a unit/semantics test; new compile-time checks need a *negative* compile test in `test/compile_fail/` (registered as a `WILL_FAIL` ctest).
- **Keep the scope tight.** ctfsm is a *control-mode* FSM engine — flat states + wildcards. Hierarchy/regions are deliberately out of scope for now (see the README/spec); please discuss in an issue before adding them.
- Update `CHANGELOG.md` and the docs (`README.md`, `docs/design.md`) for user-visible changes.

By contributing, you agree your contributions are licensed under **Apache-2.0**.
