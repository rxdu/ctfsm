// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// Tests for the safety/correctness semantics added in v0.2.0: re-entrancy
// refusal, Stop() running the active state's OnExit, and the rule that a
// guard-refused exact transition does NOT fall through to a wildcard.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ctfsm/fsm.hpp"

namespace {

// ---------- Re-entrancy ----------
struct ReCtx;
struct ReA;
struct ReB {};
struct ReEvent {};
using ReFsm = ctfsm::StateMachine<ReCtx, ctfsm::StateList<ReA, ReB>,
                                  ctfsm::Table<ctfsm::Row<ReA, ReEvent, ReB>>>;

struct ReCtx {
  ReFsm* fsm = nullptr;
  bool reentry_returned = true;  // what the nested Dispatch returned
};

struct ReA {
  void OnEnter(ReCtx& c) {
    // Illegal: drive the machine from inside a hook. Must be refused.
    if (c.fsm != nullptr) c.reentry_returned = c.fsm->Dispatch(ReEvent{}, c);
  }
};

struct ReReporter : ctfsm::FsmObserver {
  int blocked = 0;
  void OnReentrancyBlocked(std::string_view) noexcept override { ++blocked; }
};

TEST(Semantics, ReentrantDispatchIsRefusedAndReported) {
  ReReporter rep;
  ReCtx ctx;
  ReFsm fsm(&rep);
  ctx.fsm = &fsm;
  fsm.Start(ctx);                // ReA::OnEnter attempts a re-entrant Dispatch
  EXPECT_TRUE(fsm.IsIn<ReA>());  // the nested dispatch did NOT transition
  EXPECT_FALSE(ctx.reentry_returned);  // it returned false
  EXPECT_EQ(rep.blocked, 1);           // and was reported
}

// ---------- Stop() runs OnExit ----------
struct LogCtx {
  std::vector<std::string> log;
};
struct Active {
  void OnEnter(LogCtx& c) { c.log.push_back("Active.enter"); }
  void OnExit(LogCtx& c) {
    c.log.push_back("Active.exit");
  }  // e.g. zero torques
};
struct Other {};
struct Noop {};
using StopFsm =
    ctfsm::StateMachine<LogCtx, ctfsm::StateList<Active, Other>,
                        ctfsm::Table<ctfsm::Row<Active, Noop, Other>>>;

TEST(Semantics, StopRunsActiveStateOnExit) {
  LogCtx ctx;
  StopFsm fsm;
  fsm.Start(ctx);
  EXPECT_TRUE(fsm.started());
  fsm.Stop(ctx);
  EXPECT_FALSE(fsm.started());
  EXPECT_EQ(ctx.log, (std::vector<std::string>{"Active.enter", "Active.exit"}));
}

TEST(Semantics, StopIsIdempotentAndSafeBeforeStart) {
  LogCtx ctx;
  StopFsm fsm;
  fsm.Stop(ctx);  // never started -> no-op, no OnExit
  EXPECT_TRUE(ctx.log.empty());
  fsm.Start(ctx);
  fsm.Stop(ctx);
  fsm.Stop(ctx);  // second Stop -> no-op
  EXPECT_EQ(ctx.log, (std::vector<std::string>{"Active.enter", "Active.exit"}));
}

// ---------- No fall-through from a guard-refused exact transition ----------
struct FtCtx {
  bool allow = false;
};
struct S0 {};
struct S1 {};
struct S2 {};
struct Ev {};
struct Allow {
  bool operator()(const FtCtx& c) const noexcept { return c.allow; }
};
// S0 has a SPECIFIC Ev->S1 guarded by Allow, AND a wildcard Ev->S2.
using FtFsm =
    ctfsm::StateMachine<FtCtx, ctfsm::StateList<S0, S1, S2>,
                        ctfsm::Table<ctfsm::Row<S0, Ev, S1, Allow>,
                                     ctfsm::Row<ctfsm::AnyState, Ev, S2>>>;

TEST(Semantics, GuardRefusedExactDoesNotFallThroughToWildcard) {
  FtCtx ctx;  // allow == false -> the exact S0->S1 guard refuses
  FtFsm fsm;
  fsm.Start(ctx);
  EXPECT_FALSE(fsm.Dispatch(Ev{}, ctx));  // refused, and does NOT take wildcard
  EXPECT_TRUE(fsm.IsIn<S0>());            // stayed put
}

TEST(Semantics, ExactTransitionTakesWhenGuardPasses) {
  FtCtx ctx;
  ctx.allow = true;
  FtFsm fsm;
  fsm.Start(ctx);
  EXPECT_TRUE(fsm.Dispatch(Ev{}, ctx));
  EXPECT_TRUE(fsm.IsIn<S1>());  // exact wins over wildcard
}

TEST(Semantics, WildcardFiresWhenNoExactRowMatches) {
  FtCtx ctx;
  FtFsm fsm;
  fsm.Start(ctx);
  ASSERT_FALSE(fsm.Dispatch(Ev{}, ctx));  // S0+Ev refused (allow=false), stays
  // From S1 there is no exact Ev row, so the wildcard Ev->S2 applies. First get
  // into S1:
  ctx.allow = true;
  ASSERT_TRUE(fsm.Dispatch(Ev{}, ctx));  // S0 -> S1
  ASSERT_TRUE(fsm.IsIn<S1>());
  EXPECT_TRUE(fsm.Dispatch(Ev{}, ctx));  // no exact S1+Ev -> wildcard -> S2
  EXPECT_TRUE(fsm.IsIn<S2>());
}

}  // namespace
