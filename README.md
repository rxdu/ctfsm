# ctfsm

A compile-time FSM engine that turns a declarative transition table into a verified, allocation-free state machine for real-time, safety-critical robotics control.

Header-only C++17. States and the transition graph are plain types: the graph is **verified at compile time**, dispatch is **allocation-free, RTTI-free, and bounded**, and every transition/refusal is **traceable** through an observer — real-time and deterministic by construction.

## Why

Most C++ FSM choices are either runtime/polymorphic (heap, vtables, the graph only checked at runtime) or powerful-but-cryptic metaprogramming. `ctfsm` aims for a small, readable middle: the **transition table is the single source of truth** for the graph, the engine *enforces* it, and the real-time properties are explicit guarantees, not hopes.

- **Declarative table** — the whole legal graph in one place; illegal transitions aren't expressible.
- **Compile-time checks** — every `From`/`To` must be a real state (more checks planned).
- **Explicit lifecycle** — optional `OnEnter` / `Update` / `OnExit` per state.
- **Guarded transitions with reasons** — a refused transition is reported with the guard that blocked it.
- **Completion (auto) transitions** — guard-triggered, evaluated each `Update()`.
- **Wildcards** — `AnyState` rows for shared transitions (e.g. E-stop from anywhere).
- **Traceable** — an observer is notified on every enter/exit/transition/refusal (off the per-tick path).
- **RT-safe** — preallocated states, no heap, no RTTI, no exceptions; bounded, deterministic dispatch.

## Quick example

```cpp
#include "ctfsm/fsm.hpp"

struct Ctx { bool ready = false; };

struct Booting { void OnEnter(Ctx&) {} };
struct Running { void Update(Ctx&) {} };

struct WhenReady { bool operator()(const Ctx& c) const noexcept { return c.ready; } };

using Boot = ctfsm::StateMachine<
    Ctx,
    ctfsm::StateList<Booting, Running>,                       // first = initial
    ctfsm::Table<ctfsm::Row<Booting, ctfsm::Completion, Running, WhenReady>>>;

Ctx ctx;
Boot fsm;
fsm.Start(ctx);          // enters Booting
fsm.Update(ctx);         // stays until ctx.ready, then auto-advances to Running
```

External events and guarded/actioned transitions:
```cpp
struct Coin {}; struct Push {};
struct Allowed { bool operator()(const Ctx&) const noexcept { return true; } };
struct AddCoin { void operator()(Ctx&) const noexcept {} };

using Turnstile = ctfsm::StateMachine<Ctx,
    ctfsm::StateList<Locked, Unlocked>,
    ctfsm::Table<
        ctfsm::Row<Locked,   Coin, Unlocked, Allowed, AddCoin>,
        ctfsm::Row<Unlocked, Push, Locked>,
        ctfsm::Row<ctfsm::AnyState, Reset, Locked>>>;   // shared, from any state

fsm.Dispatch(Coin{}, ctx);   // -> Unlocked (if Allowed); else refused, with reason
```

> Guards and actions are **default-constructible functor types** (not lambdas — captureless lambdas aren't default-constructible in C++17). They may define `static constexpr std::string_view kName` for nicer traces.

## Integrate

Header-only. Three supported paths, all exposing the same `ctfsm::ctfsm` target:

**1. Source** — vendor (submodule/subtree) and add the subdirectory:
```cmake
add_subdirectory(third_party/ctfsm)
target_link_libraries(your_target PRIVATE ctfsm::ctfsm)
```

**2. `find_package`** — install once, then consume:
```sh
cmake -S . -B build && cmake --build build && cmake --install build --prefix /usr/local
```
```cmake
find_package(ctfsm 0.1.0 REQUIRED)
target_link_libraries(your_target PRIVATE ctfsm::ctfsm)
```

**3. Debian package** — build a `.deb` and install system-wide:
```sh
cmake -S . -B build && cd build && cpack -G DEB        # -> ctfsm_<ver>_all.deb
sudo dpkg -i ctfsm_*_all.deb                            # installs headers + CMake config
```
then use `find_package(ctfsm)` as above.

> When `ctfsm` is consumed as a subproject (`add_subdirectory`), its install/package and test/example rules default **off** (`CTFSM_INSTALL`, `CTFSM_BUILD_TESTS`, `CTFSM_BUILD_EXAMPLES`), so it doesn't pollute the parent build.

Build the tests and examples:
```sh
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

Requires a C++17 compiler. Tests use GoogleTest (fetched automatically).

## Examples

Runnable references in [`examples/`](examples/) — they narrate what they do, self-check, and are registered as CTest tests (so `ctest` runs them too):

- [`turnstile.cpp`](examples/turnstile.cpp) — the minimal "hello world": states, events, an action, an ignored event.
- [`robot_control.cpp`](examples/robot_control.cpp) — the motivating use case: a quadruped control FSM (`Passive → StandingUp → Standing → Walking`) with guarded transitions, refusal-with-reason, a completion auto-advance, a wildcard E-stop from any state, and a trace observer. Run it to see the trace:

```text
  [enter]      Passive
  [refused]    RequestStand in Passive (guard EstimatorReady not satisfied)
  [transition] Passive --RequestStand--> StandingUp
  [transition] StandingUp --Completion--> Standing
  [transition] Standing --RequestWalk--> Walking
  [transition] Walking --EStop--> Passive
```

## Status & design

v0.1.0 — flat states + wildcard rows; hierarchy/regions are a documented future extension. The full design, semantics, and real-time contract are in [`docs/design.md`](docs/design.md).

## License

Apache-2.0 © Ruixiang Du.
