// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// The motivating use case: a quadruped-style control FSM, wrapped in a
// RobotController that OWNS the machine and exposes a domain API. This is the
// production shape — the FSM (states, events, guards, the transition table, the
// trace observer) is a private mechanism; callers issue intents (Stand / Walk /
// EmergencyStop) and the loop calls Tick().
//
//   Passive --RequestStand[EstimatorReady]--> StandingUp
//   StandingUp --(completion)[StandSettled]--> Standing       (auto-advance)
//   Standing --RequestWalk[GaitReady]--> Walking
//   Walking  --RequestStand--> Standing
//   AnyState --EStop/MarkEStop--> Passive                      (E-stop
//   anywhere)
//
// Shows guarded transitions with refusal reasons, completion (auto)
// transitions, a wildcard E-stop, and a trace observer. Doubles as a CTest
// test.
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

class RobotController {
 public:
  RobotController() : fsm_(&trace_) { fsm_.Start(ctx_); }

  // Domain commands — return whether the request was accepted.
  bool Stand() { return fsm_.Dispatch(RequestStand{}, ctx_); }
  bool Walk() { return fsm_.Dispatch(RequestWalk{}, ctx_); }
  bool EmergencyStop() { return fsm_.Dispatch(EStop{}, ctx_); }

  // One control tick (in real use also reads sensors / runs the estimator).
  void Tick() { fsm_.Update(ctx_); }

  // Sensor inputs — in a real controller these are written by the estimator
  // each tick; exposed here so the scenario can drive them.
  void set_estimator_ready(bool v) { ctx_.estimator_ready = v; }
  void set_gait_ready(bool v) { ctx_.gait_ready = v; }

  // Observation.
  std::string_view state() const { return fsm_.CurrentName(); }
  const std::string& command() const { return ctx_.command; }
  int estop_count() const { return ctx_.estop_count; }

 private:
  // --- the shared blackboard (production: split into const-in / mutable-out;
  //     see docs/design.md §13). Per-state scratch lives in the states. ---
  struct Control {
    bool estimator_ready = false;
    bool gait_ready = false;
    bool stand_settled = false;  // published by StandingUp when ramped
    std::string command = "none";
    int estop_count = 0;
  };

  // --- states ---
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
      if (progress >= 100) c.stand_settled = true;
    }
  };
  struct Standing {
    void OnEnter(Control& c) { c.command = "balance"; }
  };
  struct Walking {
    void OnEnter(Control& c) { c.command = "trot"; }
  };

  // --- events ---
  struct RequestStand {};
  struct RequestWalk {};
  struct EStop {};

  // --- guards (named for self-explanatory refusal traces) ---
  struct EstimatorReady {
    static constexpr std::string_view kName = "EstimatorReady";
    bool operator()(const Control& c) const noexcept {
      return c.estimator_ready;
    }
  };
  struct GaitReady {
    static constexpr std::string_view kName = "GaitReady";
    bool operator()(const Control& c) const noexcept { return c.gait_ready; }
  };
  struct StandSettled {
    static constexpr std::string_view kName = "StandSettled";
    bool operator()(const Control& c) const noexcept { return c.stand_settled; }
  };

  // --- action ---
  struct MarkEStop {
    void operator()(Control& c) const noexcept { ++c.estop_count; }
  };

  // --- the whole legal graph, declared once and enforced by the engine ---
  // clang-format off
  using Transitions = ctfsm::Table<
    //          From              Event               To           Guard           Action
    ctfsm::Row< Passive,          RequestStand,       StandingUp,  EstimatorReady             >,
    ctfsm::Row< StandingUp,       ctfsm::Completion,  Standing,    StandSettled               >,  // auto-advance when settled
    ctfsm::Row< Standing,         RequestWalk,        Walking,     GaitReady                  >,
    ctfsm::Row< Walking,          RequestStand,       Standing                                >,
    ctfsm::Row< ctfsm::AnyState,  EStop,              Passive,     ctfsm::Always,  MarkEStop  >  // e-stop from anywhere
  >;
  // clang-format on

  using Fsm = ctfsm::StateMachine<
      Control, ctfsm::StateList<Passive, StandingUp, Standing, Walking>,
      Transitions>;

  // --- trace observer: where traceability shows up ---
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

  Control ctx_{};
  Trace trace_{};
  Fsm fsm_;  // constructed with &trace_ in the initializer list
};

}  // namespace

int main() {
  RobotController robot;

  std::cout << "start in Passive:\n";
  CHECK(robot.state() == "Passive");
  CHECK(robot.command() == "damping");

  std::cout << "request stand while the estimator is NOT ready (refused):\n";
  CHECK(!robot.Stand());
  CHECK(robot.state() == "Passive");

  std::cout << "estimator converges; request stand (accepted):\n";
  robot.set_estimator_ready(true);
  CHECK(robot.Stand());
  CHECK(robot.state() == "StandingUp");

  std::cout << "tick the stand-up ramp until it auto-advances to Standing:\n";
  for (int i = 0; i < 10 && robot.state() != "Standing"; ++i) robot.Tick();
  CHECK(robot.state() == "Standing");
  CHECK(robot.command() == "balance");

  std::cout << "request walk while gait NOT ready (refused), then ready:\n";
  CHECK(!robot.Walk());
  robot.set_gait_ready(true);
  CHECK(robot.Walk());
  CHECK(robot.state() == "Walking");

  std::cout << "E-stop from Walking (wildcard transition to Passive):\n";
  CHECK(robot.EmergencyStop());
  CHECK(robot.state() == "Passive");
  CHECK(robot.estop_count() == 1);
  CHECK(robot.command() == "damping");  // Passive.OnEnter ran after the action

  std::cout << "control scenario OK\n";
  return 0;
}
