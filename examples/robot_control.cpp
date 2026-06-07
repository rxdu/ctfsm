// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// The motivating use case: a quadruped-style control FSM.
//   Passive --RequestStand[EstimatorReady]--> StandingUp
//   StandingUp --(completion)[StandSettled]--> Standing       (auto-advance)
//   Standing --RequestWalk[GaitReady]--> Walking
//   Walking  --RequestStand--> Standing
//   AnyState --EStop/MarkEStop--> Passive                     (E-stop anywhere)
//
// Shows: guarded transitions with refusal reasons, completion (auto)
// transitions, a wildcard E-stop, and a trace observer — the features that make
// ctfsm suited to real-time, safety-critical control. Doubles as a CTest test.
#include <iostream>
#include <string>
#include <string_view>

#include "ctfsm/fsm.hpp"

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "CHECK failed: " #cond " @line " << __LINE__ << "\n"; \
      return 1;                                                          \
    }                                                                    \
  } while (0)

namespace {

// The shared blackboard. In production this would be split into a const input
// view and a mutable output sink (see docs/design.md §13); kept flat here for
// readability. Per-state scratch (e.g. the stand ramp) lives in the states.
struct Control {
  // inputs
  bool estimator_ready = false;
  bool gait_ready = false;
  bool stand_settled =
      false;  // published by StandingUp when its ramp completes
  // outputs / effects
  std::string command = "none";
  int estop_count = 0;
};

// --- States (each may carry its own scratch; StandingUp ramps a counter) ---
struct Passive {
  void OnEnter(Control& c) { c.command = "damping"; }
};
struct StandingUp {
  int progress = 0;  // per-state scratch — never leaks into the context
  void OnEnter(Control& c) {
    progress = 0;
    c.stand_settled = false;
    c.command = "stand_ramp";
  }
  void Update(Control& c) {
    progress += 25;
    if (progress >= 100) c.stand_settled = true;  // publish completion
  }
};
struct Standing {
  void OnEnter(Control& c) { c.command = "balance"; }
};
struct Walking {
  void OnEnter(Control& c) { c.command = "trot"; }
};

// --- Events ---
struct RequestStand {};
struct RequestWalk {};
struct EStop {};

// --- Guards (named, so refusals are self-explanatory in the trace) ---
struct EstimatorReady {
  static constexpr std::string_view kName = "EstimatorReady";
  bool operator()(const Control& c) const noexcept { return c.estimator_ready; }
};
struct GaitReady {
  static constexpr std::string_view kName = "GaitReady";
  bool operator()(const Control& c) const noexcept { return c.gait_ready; }
};
// Completion guard for the auto-advance: StandingUp keeps its ramp progress as
// per-state scratch and publishes a single "settled" flag to the context; the
// guard reads that. (Guards see only the context, never the state — which is
// exactly how a state signals completion cleanly.)
struct StandSettled {
  static constexpr std::string_view kName = "StandSettled";
  bool operator()(const Control& c) const noexcept { return c.stand_settled; }
};

// --- Action: record an E-stop (OnEnter of Passive still sets the command) ---
struct MarkEStop {
  void operator()(Control& c) const noexcept { ++c.estop_count; }
};

// --- The whole legal graph, declared once and enforced by the engine ---
using Transitions = ctfsm::Table<
    ctfsm::Row<Passive, RequestStand, StandingUp, EstimatorReady>,
    ctfsm::Row<StandingUp, ctfsm::Completion, Standing, StandSettled>,
    ctfsm::Row<Standing, RequestWalk, Walking, GaitReady>,
    ctfsm::Row<Walking, RequestStand, Standing>,
    ctfsm::Row<ctfsm::AnyState, EStop, Passive, ctfsm::Always, MarkEStop>>;

using ControlFsm = ctfsm::StateMachine<
    Control, ctfsm::StateList<Passive, StandingUp, Standing, Walking>,
    Transitions>;

// --- A trace observer — this is where traceability shows up ---
struct Trace : ctfsm::FsmObserver {
  void OnEnter(std::string_view s) noexcept override {
    std::cout << "  [enter]      " << s << "\n";
  }
  void OnTransition(std::string_view f, std::string_view e,
                    std::string_view t) noexcept override {
    std::cout << "  [transition] " << f << " --" << e << "--> " << t << "\n";
  }
  void OnRefused(std::string_view f, std::string_view e,
                 std::string_view g) noexcept override {
    std::cout << "  [refused]    " << e << " in " << f << " (guard " << g
              << " not satisfied)\n";
  }
  void OnIgnored(std::string_view f, std::string_view e) noexcept override {
    std::cout << "  [ignored]    " << e << " in " << f << "\n";
  }
};

// One control tick. In a real loop this is also where sensors are read and the
// estimator runs; here it just drives the FSM's current-state Update() (which
// advances the stand ramp and may fire a completion transition).
void Tick(ControlFsm& fsm, Control& ctx) { fsm.Update(ctx); }

}  // namespace

int main() {
  Control ctx;
  Trace trace;
  ControlFsm fsm(&trace);

  std::cout << "start in Passive:\n";
  fsm.Start(ctx);
  CHECK(fsm.IsIn<Passive>());
  CHECK(ctx.command == "damping");

  std::cout << "request stand while the estimator is NOT ready (refused):\n";
  CHECK(!fsm.Dispatch(RequestStand{}, ctx));
  CHECK(fsm.IsIn<Passive>());

  std::cout << "estimator converges; request stand (accepted):\n";
  ctx.estimator_ready = true;
  CHECK(fsm.Dispatch(RequestStand{}, ctx));
  CHECK(fsm.IsIn<StandingUp>());

  std::cout << "tick the stand-up ramp until it auto-advances to Standing:\n";
  for (int i = 0; i < 10 && !fsm.IsIn<Standing>(); ++i) {
    Tick(fsm,
         ctx);  // StandingUp.Update ramps; StandSettled fires the auto-advance
  }
  CHECK(fsm.IsIn<Standing>());
  CHECK(ctx.command == "balance");

  std::cout << "request walk while gait NOT ready (refused), then ready:\n";
  CHECK(!fsm.Dispatch(RequestWalk{}, ctx));
  ctx.gait_ready = true;
  CHECK(fsm.Dispatch(RequestWalk{}, ctx));
  CHECK(fsm.IsIn<Walking>());

  std::cout << "E-stop from Walking (wildcard transition to Passive):\n";
  CHECK(fsm.Dispatch(EStop{}, ctx));
  CHECK(fsm.IsIn<Passive>());
  CHECK(ctx.estop_count == 1);      // the E-stop action ran
  CHECK(ctx.command == "damping");  // Passive.OnEnter ran after the action

  std::cout << "control scenario OK\n";
  return 0;
}
