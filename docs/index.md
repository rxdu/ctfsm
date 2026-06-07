# ctfsm

**A compile-time FSM engine that turns a declarative transition table into a verified, allocation-free state machine for real-time, safety-critical robotics control.**

[View on GitHub](https://github.com/rxdu/ctfsm) · [Design spec](./design) · [Changelog](https://github.com/rxdu/ctfsm/blob/main/CHANGELOG.md) · Apache-2.0

`ctfsm` is a small, header-only C++17 library for the one job a robot's high-level controller can't get wrong: **deciding what state the machine is in and when it may change.** States and the transition graph are ordinary types, so the graph is *verified by the compiler*; the runtime is allocation-free with no RTTI or exceptions, so it fits inside a hard real-time control loop; and every transition is traceable, so when a robot misbehaves you can reconstruct why.

---

## Why another FSM library

Most C++ state machines fall into one of two camps:

- **Runtime / polymorphic** (virtual `State` classes, `std::function` tables): readable, but heap-allocating, vtable-dispatched, and the transition graph is only checked when you hit a bad path at runtime.
- **Heavyweight metaprogramming** (e.g. Boost.SML): powerful and compile-time, but a large abstraction with famously cryptic errors.

`ctfsm` aims for a small, readable middle, tuned for control:

> The transition table is the single source of truth, the compiler verifies it, and the real-time properties are *proven* — not hoped for.

---

## Design philosophy

**1. The table is the single source of truth.** The entire legal graph is declared once, as data. The engine is the *only* path that changes state — there is no `goto`-equivalent hiding in a state's code. You can review the whole safety graph in one place.

**2. Verify at compile time.** Because states and transitions are types, the compiler enforces the graph: a typo'd state, an *unreachable* state, or two rows sharing the same `(From, Event, Guard)` key are **build errors**. The verification is itself tested — the repo ships *negative compile tests* that fail the build if a check stops firing.

**3. Real-time by construction.** Value-type states held in a preallocated tuple; no heap, no RTTI, no exceptions; bounded, deterministic dispatch. These aren't claims — they're regression-guarded tests: **0 allocations across 100,000 ticks**, a determinism check, and a build under `-fno-exceptions -fno-rtti`.

**4. Own your semantics.** The engine is a few hundred lines you can read top to bottom. Entry/exit/transition ordering, guard evaluation, and resolution rules are explicit and documented — no hidden framework behavior to reverse-engineer when something surprises you.

**5. Fail safe, stay observable.** A re-entrant or illegal call is *refused*, never aborts — dropping a control loop is worse than refusing one nested call. Every transition, refusal (with the guard that blocked it), and refusal-reason flows to an observer you can wire to your trace bus.

---

## How it works

A machine is three things: a **context** (your blackboard), a **`StateList`** (the states; first is initial), and a **`Table`** of **`Row`s** (the graph). States are plain structs with *optional* `OnEnter` / `Update` / `OnExit` hooks.

```cpp
#include <iostream>

#include "ctfsm/fsm.hpp"

struct Ctx { bool homed = false; };

struct Idle    { void OnEnter(Ctx&) { std::cout << "idle\n"; } };
struct Running { void Update(Ctx&)  { /* per-tick control action */ } };
struct Fault   { void OnEnter(Ctx&) { std::cout << "FAULT\n"; } };

struct Start {};  struct Trip {};                       // events (tags)
struct Homed { bool operator()(const Ctx& c) const noexcept { return c.homed; } };

using Machine = ctfsm::StateMachine<Ctx,
    ctfsm::StateList<Idle, Running, Fault>,                 // first = initial
    ctfsm::Table<
        ctfsm::Row<Idle,            Start, Running, Homed>, // guarded transition
        ctfsm::Row<ctfsm::AnyState, Trip,  Fault>>>;        // wildcard: from anywhere

int main() {
  Ctx ctx;
  Machine fsm;
  fsm.Start(ctx);                 // enter Idle
  fsm.Dispatch(Start{}, ctx);     // refused — guard Homed is false
  ctx.homed = true;
  fsm.Dispatch(Start{}, ctx);     // Idle -> Running
  fsm.Update(ctx);                // Running.Update()
  fsm.Dispatch(Trip{}, ctx);      // -> Fault (wildcard)
  fsm.Stop(ctx);                  // run the active state's OnExit
}
```

Guards and actions are **default-constructible functor types** (not lambdas — captureless lambdas aren't default-constructible in C++17); give them a `static constexpr std::string_view kName` for readable traces.

---

## Real-time, and *proven*

The properties that let `ctfsm` live in a control loop are backed by tests, not prose:

| Property | Evidence |
|---|---|
| Allocation-free hot path | A global `operator new` counter shows **0 allocations / 100k ticks** (incl. a transition each iteration) |
| No vtable | `static_assert(!is_polymorphic_v<Machine>)` |
| No exceptions on the API | `static_assert(noexcept(...))` on `Start`/`Stop`/`Update`/`Dispatch` |
| No RTTI / no exceptions needed | a test built with `-fno-exceptions -fno-rtti` |
| Deterministic | identical input → identical state trajectory |
| Verified graph | negative compile tests for unreachable & ambiguous tables |

Dispatch is `O(applicable rows)` with the event match resolved at compile time — bounded and deterministic, with no syscalls and no unbounded loops.

---

## Semantics that matter for control

- **Lifecycle.** `Start(ctx)` enters the initial state. `Stop(ctx)` runs the active state's `OnExit` — your safe-shutdown hook (e.g. ramp torques to zero), which a destructor-only design silently skips.
- **Guards with reasons.** A blocked transition reports the guard that refused it. A guard-blocked *specific* transition does **not** silently fall through to a wildcard — "no" means no.
- **Completion (auto) transitions.** A `Row<S, Completion, …, Guard>` advances inside `Update()` when its guard passes — for "stand-up ramp finished → Standing".
- **Wildcards.** `Row<AnyState, EStop, Passive>` gives you E-stop-from-anywhere in one line.
- **Re-entrancy & threading.** Single-threaded and non-reentrant by contract: deliver cross-thread events through your own lock-free queue and `Dispatch` them on the control thread; a hook must not drive its own machine (it's refused and reported).

### A control FSM, traced

The [`robot_control` example](https://github.com/rxdu/ctfsm/blob/main/examples/robot_control.cpp) wraps a quadruped controller's FSM in a `RobotController` class and prints its observer:

```text
  [enter]      Passive
  [refused]    RequestStand in Passive (guard EstimatorReady not satisfied)
  [transition] Passive --RequestStand--> StandingUp
  [transition] StandingUp --Completion--> Standing
  [transition] Standing --RequestWalk--> Walking
  [transition] Walking --EStop--> Passive
```

---

## Integrate

Header-only; three paths, all exposing `ctfsm::ctfsm`:

```cmake
# 1) source:        add_subdirectory(third_party/ctfsm)
# 2) find_package:  cmake --install build --prefix /usr/local; then:
find_package(ctfsm 0.2.0 REQUIRED)
target_link_libraries(your_target PRIVATE ctfsm::ctfsm)
# 3) deb:           (cd build && cpack -G DEB) -> sudo dpkg -i ctfsm_*_all.deb
```

As a subproject (`add_subdirectory`), `ctfsm`'s tests/examples/install rules default **off**.

---

## Scope & limitations (v0.2.0)

Deliberate and documented, not lurking:

- No hierarchy / regions / history — shared transitions use `AnyState`; nesting and resume-state are future work.
- One completion transition per `Update` (bounded; no cascade).
- Events are *types*, not values — carry per-event data in the context.
- Refusal reasons reach the observer, not the `bool` return.
- Not a certified-safety (MISRA / DO-178) subset — fine for industrial robotics; a separate effort for certified contexts.

---

Full design and the precise real-time contract: **[design spec](./design)**. Source, issues, and releases: **[github.com/rxdu/ctfsm](https://github.com/rxdu/ctfsm)**.
