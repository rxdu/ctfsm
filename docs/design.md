# Spec — `ctfsm`, an owned compile-time state-machine framework

- **Status:** Implemented (v0.2.0)
- **Owns:** the generic FSM engine (this repo). Replaces the dependency on `xmotion`'s `fsm_template`. xmotion stays out unless a later, unrelated need (HID device I/O) brings it back.
- **Realizes:** the production-FSM requirements discussed — explicit state lifecycle, a declared+enforced transition graph, guarded transitions with refusal reasons, trace hooks, and compile-time verification — while keeping the prior design's real-time purity (no heap, value-type states).

---

## 1. Goals & non-goals

**Goals**
- A small, reusable, **robot-agnostic** state-machine engine (knows nothing about control/robots). The control state machine is an *application* of it.
- **Real-time safe:** no heap allocation, no RTTI, no exceptions on the hot path; bounded, deterministic dispatch; all states preallocated.
- **Safety graph as data:** the set of legal transitions is a declared table the engine enforces — an illegal transition is not expressible, and the graph is auditable in one place and checkable at compile time.
- **Observable:** every enter/exit/transition/refusal is reported to a trace observer.
- **Readable:** a newcomer can read the engine (~a few hundred lines) and a state's behavior + its transitions without chasing an external template.

**Non-goals (v1 — deliberately scoped tight)**
- No hierarchical/nested states, no orthogonal regions, no history states. Shared transitions (e.g. E-stop from anywhere) are handled by a **wildcard `From`**, not hierarchy. (Hierarchy is a documented future extension, §10.)
- Not thread-safe. The FSM is owned by the control loop; external events arrive via the loop's lock-free input queue (§9).
- Not a general event bus. Events are dispatched synchronously into the running loop.

---

## 2. Concepts

| Concept | What it is | Notes |
|---------|-----------|-------|
| **Context** | User blackboard type (e.g. `ControlContext`), passed by ref to every hook | The engine is templated on it; it never inspects its fields. A **non-owning façade** of references, not a data dump — see §13 for how it's kept from bloating. |
| **State** | A value type with optional lifecycle hooks | `OnEnter(Context&)`, `Update(Context&)`, `OnExit(Context&)`. Missing hooks → no-op (detected via traits). May hold per-state data (timers, interpolation progress). |
| **Event** | A (usually empty) tag type, e.g. `struct RequestStand{}` | External request. Carries no payload in v1 — payload lives in `Context` (§8 Q1). |
| **Guard** | Stateless predicate `bool(const Context&)` | Precondition on a transition. Fails → transition refused (with reason). Default `Always`. |
| **Action** | Stateless effect `void(Context&)` | Runs *during* a transition (ordering in §5). Default none. |
| **Row** | One legal transition: `Row<From, Event, To, Guard=Always, Action=NoAction>` | `From` may be a state or the wildcard `AnyState`. |
| **Table** | `Table<Row<...>, Row<...>, ...>` | The single source of truth for the graph. |
| **Completion event** | Framework tag `Completion` used as an `Event` for *automatic* (no external event) transitions | Evaluated each `Update` (§5), e.g. "ramp finished → next state". |

States, events, guards, actions are all **plain types**; the table is a **type**. Nothing is allocated.

---

## 3. Engine API

Header-only (all templates). Sketch:

```cpp
namespace ctfsm {

template <class... States> struct StateList {};      // first = initial state

struct AnyState {};                                  // wildcard From
struct Completion {};                                // auto-transition event
struct Always   { bool operator()(const auto&) const noexcept { return true; } };
struct NoAction { void operator()(auto&)       const noexcept {} };

template <class From, class Event, class To,
          class Guard = Always, class Action = NoAction>
struct Row {};

template <class... Rows> struct Table {};

template <class Context, class States, class Transitions>
class StateMachine {
 public:
  explicit StateMachine(FsmObserver* observer = nullptr) noexcept;

  void Start(Context& ctx) noexcept;                 // enter the initial state (OnEnter)
  void Stop(Context& ctx) noexcept;                  // run the active state's OnExit (shutdown)
  void Update(Context& ctx) noexcept;                // current.Update(), then auto-transitions

  template <class Event>
  bool Dispatch(const Event& ev, Context& ctx) noexcept;   // process an external event

  template <class State> bool IsIn() const noexcept; // introspection
  bool started() const noexcept;
  std::string_view CurrentName() const noexcept;     // for per-tick trace
  std::size_t CurrentId() const noexcept;
};

}  // namespace ctfsm
```

The public API is unconditionally `noexcept` — a throwing user hook therefore hits the `noexcept` boundary and calls `std::terminate` (fail-stop; keep hooks `noexcept`). State storage is a preallocated `std::tuple<States...>` member (§7). The engine is **non-reentrant**: a hook must not call `Start`/`Stop`/`Update`/`Dispatch` on its own machine — such calls are refused (no state change) and reported via `OnReentrancyBlocked`.

---

## 4. The transition table (the centerpiece)

```cpp
using ControlTransitions = ctfsm::Table<
  //   From            Event          To        Guard            Action
  Row< Initialization, Completion,    Passive >,
  Row< Passive,        RequestStand,  StandUp,  EstimatorReady >,
  Row< StandUp,        Completion,    Stand,    StandSettled >,
  Row< Stand,          RequestWalk,   Walk,     GaitReady,       ZeroIntegrators >,
  Row< Walk,           RequestStand,  Stand >,
  Row< AnyState,       EStop,         Passive >                  // shared, from any state
>;
```

The whole legal graph is right here, reviewable. The engine **only** ever takes transitions in this table — there is no other path to change state.

---

## 5. Semantics & ordering (the precise contract)

**`Update(ctx)`** — once per control tick:
1. Call the current state's `Update(ctx)` (the per-tick control action; reads `ctx.in.*`, writes `ctx.out.joints`).
2. Evaluate **completion transitions** for the current state: scan `Row<Current, Completion, …>` rows in declaration order; take the first whose guard passes (runs the transition sequence below). At most one auto-transition per `Update`.

**`Dispatch(event, ctx)`** — external event:
1. **Exact pass:** scan concrete-`From` rows for this event in declaration order; the first whose **guard passes** wins → run the transition sequence; return `true`.
2. **No fall-through on refusal:** if an exact row matched the `(state, event)` but its guard(s) refused, **stop** — return `false` and emit `OnRefused{from, event, guard}` (first refusing guard). A declared, guard-blocked transition does *not* silently fall through to a wildcard.
3. **Wildcard pass:** only if **no exact row matched** the `(state, event)` at all, scan `AnyState` rows (declaration order, first passing guard wins). Take it → `true`.
4. If nothing applied: `OnRefused` if a wildcard guard refused, else `OnIgnored{from, event}`; return `false`.

**Transition sequence** (taken by both paths): `current.OnExit(ctx)` → `Action{}(ctx)` → switch current index → `next.OnEnter(ctx)` → `OnTransition{from, event, to}`. Exactly one `OnExit`/`OnEnter` pair; ordering is fixed and documented (no hidden xmotion semantics).

Determinism: resolution is a pure function of (current state, event type, guard results, declaration order). No ambiguity at runtime.

---

## 6. Compile-time guarantees (`static_assert`)

The table being *types* lets the engine prove properties at build time — all implemented, and covered by negative compile tests in `test/compile_fail/`:
- Every `From`/`To` is a state in `StateList` (or `AnyState` for `From`) — no typo'd/orphan states.
- Every state in `StateList` is **reachable** — the initial state or some row's `To`. Unreachable → build error.
- **No two rows share a `(From, Event, Guard)` key** → build error. (Same `From`/`Event` with *different* guards is allowed — first passing guard wins; identical keys are unresolvable ambiguity.)

These turn "I hope every transition respects the graph" into "the graph is verified before the binary exists." (Dead-row detection beyond key-ambiguity and terminal-state marking are future work.)

---

## 7. State storage & dispatch (RT model)

- States are stored **preallocated**, one instance each, in a `std::tuple<States...>` owned by the engine. A transition is an **index change + OnExit/OnEnter calls** — it never constructs, destructs, or allocates. State data persists between visits; `OnEnter` (re)initializes it.
- Dispatching `Update` to the current state is **one indirect call** through a compile-time-generated jump (think `std::visit` over the state set), *not* a vtable lookup.

> **Honest framing of the RT win:** the advantage over a polymorphic/virtual FSM is **not** "zero indirect calls" — there is still one dispatch per tick. The wins are: value-type states (no heap, no object lifetimes to manage), a **compile-time-verified transition graph**, owned/explicit semantics, and no RTTI. That's what "production-grade + safety-critical" actually buys here.

---

## 8. Observability

A lightweight observer the engine notifies; **only on transitions/events, never per-tick**, so it's off the RT-critical path:

```cpp
struct FsmObserver {
  virtual void OnEnter (std::string_view state) noexcept {}
  virtual void OnExit  (std::string_view state) noexcept {}
  virtual void OnTransition(std::string_view from, std::string_view ev, std::string_view to) noexcept {}
  virtual void OnRefused   (std::string_view from, std::string_view ev, std::string_view guard) noexcept {}
  virtual void OnIgnored   (std::string_view from, std::string_view ev) noexcept {}
  virtual ~FsmObserver() = default;
};
```

- The control loop wires this to the **trace bus** (Standards §6); the observer just records, non-blocking.
- Per-tick "which state are we in" is captured cheaply by the loop via `CurrentId()` — no observer call needed.
- **Names** (`state`/`ev`/`guard`) come from a compile-time type-name helper (no RTTI), overridable per type with `static constexpr std::string_view kName`.

---

## 9. Threading

Single-threaded by design. The control loop owns the FSM and calls `Update`/`Dispatch`. User-input events produced on another thread are pushed into the loop's **lock-free SPSC queue** and drained at the top of each tick, each drained event handed to `Dispatch`. The framework itself takes no locks and is not shared.

---

## 10. File layout & future extensions

```
include/ctfsm/
  fsm.hpp              # umbrella include
  state_machine.hpp    # the engine
  transition_table.hpp # Row / Table / AnyState / Completion / Always / NoAction
  observer.hpp         # FsmObserver
  traits.hpp           # hook detection + compile-time type names
```
The **control** state machine (ControlContext, the concrete states, the table, `using ControlFsm = …`) is a separate application built on this, living with the control code — the framework has no `legged::control` dependency.

**Future (not v1):** hierarchical states (nested + entry/exit cascades), event payloads, history (resume) states, multiple regions. The table-of-types design extends to these; they're omitted now to keep the engine small and debuggable.

---

## 11. Worked example (control FSM on top of the framework)

```cpp
// Inputs a state may READ this tick — all const; references to frames/services owned by the loop.
struct ControlInput {
  double               time{};
  const SensorData&    sensors;
  const StateEstimate& estimate;     // produced by the estimator; states only read
  const UserCommand&   command;
  const RobotModel&    model;        // a service interface, not data
  const SystemConfig&  config;
};
// Effects a state may CAUSE.
struct ControlOutput {
  JointCommand& joints;              // the one thing states produce
  TraceSink&    trace;
};
struct ControlContext { ControlInput in; ControlOutput out; };   // a thin, non-owning façade (§13)

struct EStop {}; struct RequestStand {}; struct RequestWalk {};
constexpr auto EstimatorReady =
    [](const ControlContext& c) noexcept { return c.in.estimate.valid; };

struct Passive {
  void OnEnter(ControlContext&) noexcept;
  void Update(ControlContext& c) noexcept;   // writes c.out.joints (damping / zero torque)
};
// StandUp, Stand, Walk, Initialization ... (each holds its own per-state scratch, e.g. a ramp timer)

using ControlFsm = ctfsm::StateMachine<
    ControlContext,
    ctfsm::StateList<Initialization, Passive, StandUp, Stand, Walk>,
    ControlTransitions>;   // the table from §4
```

Control loop usage:
```cpp
fsm.Start(ctx);
while (running) {
  drain_input_into(ctx, events);          // §9
  for (auto& e : events) std::visit([&](auto ev){ fsm.Dispatch(ev, ctx); }, e);
  fsm.Update(ctx);                        // runs the current state's control action
  clamp_and_write(ctx.out.joints);        // to the HAL
  trace.RecordState(fsm.CurrentId());
}
```

---

## 12. Open questions (please mark up)

1. **Events: tag-only vs payload.** Recommend **tag-only** (data like commanded velocity lives in `ctx.command`), which keeps the table clean and events trivially comparable. Accept, or do you want events to carry data (e.g. `RequestWalk{velocity}`)?
2. **Guard reasons.** Recommend the refusal trace names the **guard type** (via the compile-time name helper) — cheap and automatic. Sufficient, or do you want guards to return a richer reason (enum/string)?
3. **Initial state.** Recommend "first in `StateList`". Or an explicit `Initial<State>` marker for clarity?
4. **Auto-transitions via `Completion`** evaluated inside `Update` — accept, or keep `Update` purely the control action and require an explicit internal `Dispatch(Completion{})` from the state?
5. **Engine implementation substrate:** `std::variant`-of-states + `std::visit`, or `std::tuple`-of-states + index + generated jump table? (Both meet the RT contract; tuple+index never constructs on transition, which I lean toward — §7.)

---

## 13. Context design — avoiding blackboard bloat

A shared context rots into a god-object when it conflates **four kinds of "state,"** each with different ownership and mutability. Only two of them belong in the context, and only as references:

| Kind | Example | Owner | In the context? |
|------|---------|-------|-----------------|
| **Per-state scratch** | a stand-up ramp timer, interpolation progress | the state object | **No** — it's a member of the state |
| **Cross-state signaling** | the old `initialized` flag | nobody (ad-hoc) | **No** — use an event or a subsystem-status guard |
| **Tick I/O** | sensors, command (in); joint command (out) | frames owned by the control loop | Yes — **by reference**, in `in`/`out` |
| **Services** | robot model, estimator, trace, config | long-lived subsystems | Yes — **by handle** (an interface ref) |

Because the engine calls `Update(ctx)` *uniformly*, safety can't come from giving each state a narrower argument — it has to come from the **context's type**. Hence the `ControlInput` (all `const`) / `ControlOutput` split in §11: a state physically cannot mutate sensors/estimate/command (compile error), only `out.joints`/`out.trace`. The context is a thin **non-owning façade** of references — it owns nothing, copies no data, and holds no per-state scratch.

**Why this stays bounded:** a new capability adds *one* handle (`const TerrainMap&`) to a service interface — not a blob of fields. Per-state data never appears (it lives in the state). Cross-state coupling goes through events/guards, not fields. Growth is "one reference per genuinely-new subsystem," and if services multiply, group them (`in.robot`, `in.world`) rather than flattening.

**Litmus test before adding a context field:**
1. Per-state scratch? → put it in the state object.
2. One state signaling another? → event or subsystem-status guard, not a field.
3. A long-lived subsystem? → add a `const Iface&` handle (interface, not concrete type).
4. The current tick's I/O frame? → reference it in `in`/`out` with the right const-ness.
5. None of the above? → it probably doesn't belong in the context.

**Guardrails:** don't over-segregate (ten one-method interfaces is its own bloat — group into a few cohesive service aggregates); and the context **owns nothing** — if you're storing a value rather than referencing one, it has an owner elsewhere that the context should point at instead. (This is also why the earlier *state-type lift* mattered: `SensorData`/`StateEstimate`/`JointCommand` being standalone types is what lets the context reference them without dragging subsystems in.)
