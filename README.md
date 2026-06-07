# ctfsm

**A compile-time FSM engine that turns a declarative transition table into a verified, allocation-free state machine for real-time, safety-critical robotics control.**

Header-only C++17. States and the transition graph are plain types: the graph is verified at *compile time*, dispatch is allocation-free with no RTTI or exceptions, and every transition is traceable — real-time and deterministic by construction.

## Design philosophy

- **The table is the single source of truth.** The whole legal graph is declared once, as data; the engine is the *only* path that changes state. No transition lives anywhere else.
- **Verify at compile time.** Typo'd states, unreachable states, and ambiguous rows are *build errors*, not runtime surprises — the safety graph is checked before the binary exists.
- **Real-time by construction.** Value-type states, no heap, no RTTI, no exceptions, bounded deterministic dispatch — *proven* by tests (0 allocations / 100k ticks) and a `-fno-exceptions -fno-rtti` build, not just claimed.
- **Own your semantics.** A small engine you can read top to bottom, with an explicit, documented lifecycle and transition ordering — no hidden framework behavior.
- **Fail safe, stay observable.** A re-entrant or illegal call is refused, never aborts; every transition/refusal/refusal-reason flows to an observer.

## Features

- Compile-time-verified transition table — reachability + `(From,Event,Guard)` ambiguity checks.
- Per-state `OnEnter` / `Update` / `OnExit` (all optional); `Start` / `Stop` lifecycle (`Stop` runs the active state's `OnExit` — your shutdown cleanup).
- **Guards** with refusal reasons; **actions**; **completion** (auto) transitions; **`AnyState`** wildcards for shared transitions (e.g. E-stop from anywhere).
- Trace **observer** (notified only on transitions — off the per-tick path).
- Three integration paths (source / `find_package` / `.deb`), all exposing `ctfsm::ctfsm`.

## Example

```cpp
#include <iostream>

#include "ctfsm/fsm.hpp"

struct Ctx { bool homed = false; };

// States — value types; every hook is optional.
struct Idle    { void OnEnter(Ctx&) { std::cout << "idle\n"; } };
struct Running { void Update(Ctx&)  { /* per-tick control action */ } };
struct Fault   { void OnEnter(Ctx&) { std::cout << "FAULT\n"; } };

// Events (tags) and a guard (a default-constructible functor — not a lambda).
struct Start {};  struct Trip {};
struct Homed { bool operator()(const Ctx& c) const noexcept { return c.homed; } };

using Machine = ctfsm::StateMachine<Ctx,
    ctfsm::StateList<Idle, Running, Fault>,                 // first = initial state
    ctfsm::Table<
        ctfsm::Row<Idle,            Start, Running, Homed>, // guarded: only if homed
        ctfsm::Row<ctfsm::AnyState, Trip,  Fault>>>;        // wildcard: trip from anywhere

int main() {
  Ctx ctx;
  Machine fsm;
  fsm.Start(ctx);                 // enter Idle
  fsm.Dispatch(Start{}, ctx);     // refused — guard Homed is false
  ctx.homed = true;
  fsm.Dispatch(Start{}, ctx);     // Idle -> Running
  fsm.Update(ctx);                // Running.Update()
  fsm.Dispatch(Trip{}, ctx);      // -> Fault (wildcard, from any state)
  fsm.Stop(ctx);                  // run the active state's OnExit
}
```

Richer references in [`examples/`](examples/) (a turnstile and a quadruped control FSM, both runnable and self-checking).

## Integrate

Header-only; pick one (all give `ctfsm::ctfsm`):

```cmake
# 1) source: vendor + add_subdirectory(third_party/ctfsm)
# 2) find_package: cmake --install build --prefix /usr/local; then:
find_package(ctfsm 0.2.0 REQUIRED)
target_link_libraries(your_target PRIVATE ctfsm::ctfsm)
# 3) deb: (cd build && cpack -G DEB) -> sudo dpkg -i ctfsm_*_all.deb
```

Build & test: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build`. As a subproject, tests/examples/install rules default off.

## More

Design, semantics, and the real-time contract: [`docs/design.md`](docs/design.md). Release history and known limitations: [`CHANGELOG.md`](CHANGELOG.md). Single-threaded and non-reentrant by contract; one completion transition per `Update`; events are types (data via the context); not a certified-safety (MISRA/DO-178) subset.

## License

Apache-2.0 © Ruixiang Du.
