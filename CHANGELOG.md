# Changelog

All notable changes to `ctfsm`. Versions follow [SemVer](https://semver.org/); pre-1.0 minor bumps may include breaking changes.

## [0.2.0] — 2026-06-07

Production-hardening for use as a real-time control FSM (humanoid/quadruped).

### Added
- `Stop(ctx)` runs the active state's `OnExit` — the documented safe-shutdown hook (e.g. ramp torques to zero). `started()` query.
- Compile-time graph verification: **unreachable-state** detection and **ambiguous-row** detection (no two rows may share a `(From, Event, Guard)` key), backed by negative compile tests.
- Re-entrancy detection: a re-entrant `Start`/`Stop`/`Update`/`Dispatch` (a hook driving its own machine) is **refused** with no state change and reported via the new `FsmObserver::OnReentrancyBlocked` — it never aborts the loop.

### Changed
- **Transition resolution (behavior change):** a guard-blocked *exact* transition no longer falls through to a wildcard for the same event. Wildcard (`AnyState`) rows apply only when no exact row matches the `(state, event)` at all.

### Notes
- Documented deliberate limitations: no hierarchy/regions/history, one completion transition per `Update`, type-only events, `noexcept` fail-stop on a throwing hook.

## [0.1.0] — 2026-06-07

Initial release: a compile-time, allocation-free finite-state-machine engine — declarative transition table, optional `OnEnter`/`Update`/`OnExit` hooks, guards/actions, completion + wildcard transitions, and a trace observer. Verified real-time properties (0 allocations, no RTTI/exceptions, deterministic). Tests, reference examples, and source / `find_package` / `.deb` integration.
