// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ctfsm/fsm.hpp"

namespace {

// ---- A context that records lifecycle calls so ordering can be asserted. ----
struct Ctx {
  std::vector<std::string> log;
  int coins = 0;
  bool allow = true;   // knob for the guard below
  bool ready = false;  // knob for the completion guard
};

// ---- Turnstile states ----
struct Locked {
  void OnEnter(Ctx& c) { c.log.emplace_back("Locked.enter"); }
  void Update(Ctx& c) { c.log.emplace_back("Locked.update"); }
  void OnExit(Ctx& c) { c.log.emplace_back("Locked.exit"); }
};
struct Unlocked {
  void OnEnter(Ctx& c) { c.log.emplace_back("Unlocked.enter"); }
  void OnExit(Ctx& c) { c.log.emplace_back("Unlocked.exit"); }
  // intentionally no Update() — proves the hook is optional
};

// ---- Events ----
struct Coin {};
struct Push {};
struct Reset {};

// ---- Guard + action (functor structs; default-constructible) ----
struct Allowed {
  static constexpr std::string_view kName = "Allowed";
  bool operator()(const Ctx& c) const noexcept { return c.allow; }
};
struct AddCoin {
  void operator()(Ctx& c) const noexcept { ++c.coins; }
};

using Turnstile = ctfsm::StateMachine<
    Ctx, ctfsm::StateList<Locked, Unlocked>,
    ctfsm::Table<ctfsm::Row<Locked, Coin, Unlocked, Allowed, AddCoin>,
                 ctfsm::Row<Unlocked, Push, Locked>,
                 ctfsm::Row<ctfsm::AnyState, Reset, Locked>>>;

// ---- Recording observer ----
struct Recorder : ctfsm::FsmObserver {
  std::vector<std::string> events;
  void OnEnter(std::string_view s) noexcept override {
    events.emplace_back(std::string("enter:") + std::string(s));
  }
  void OnTransition(std::string_view f, std::string_view e,
                    std::string_view t) noexcept override {
    events.emplace_back(std::string(f) + "-" + std::string(e) + "->" +
                        std::string(t));
  }
  void OnRefused(std::string_view f, std::string_view e,
                 std::string_view g) noexcept override {
    events.emplace_back("refused:" + std::string(f) + "/" + std::string(e) +
                        "/" + std::string(g));
  }
  void OnIgnored(std::string_view f, std::string_view e) noexcept override {
    events.emplace_back("ignored:" + std::string(f) + "/" + std::string(e));
  }
};

TEST(Ctfsm, StartsInInitialStateAndEnters) {
  Ctx ctx;
  Turnstile fsm;
  fsm.Start(ctx);
  EXPECT_TRUE(fsm.IsIn<Locked>());
  EXPECT_EQ(fsm.CurrentName(), "Locked");
  EXPECT_EQ(ctx.log, (std::vector<std::string>{"Locked.enter"}));
}

TEST(Ctfsm, UpdateRunsCurrentStateAction) {
  Ctx ctx;
  Turnstile fsm;
  fsm.Start(ctx);
  fsm.Update(ctx);
  EXPECT_EQ(ctx.log.back(), "Locked.update");
}

TEST(Ctfsm, TransitionRunsExitActionEnterInOrder) {
  Ctx ctx;
  Turnstile fsm;
  fsm.Start(ctx);
  ctx.log.clear();
  EXPECT_TRUE(fsm.Dispatch(Coin{}, ctx));
  EXPECT_TRUE(fsm.IsIn<Unlocked>());
  EXPECT_EQ(ctx.coins, 1);  // the action ran
  EXPECT_EQ(ctx.log,
            (std::vector<std::string>{"Locked.exit", "Unlocked.enter"}));
}

TEST(Ctfsm, GuardRefusalKeepsStateAndReportsReason) {
  Ctx ctx;
  ctx.allow = false;
  Recorder rec;
  Turnstile fsm(&rec);
  fsm.Start(ctx);
  EXPECT_FALSE(fsm.Dispatch(Coin{}, ctx));
  EXPECT_TRUE(fsm.IsIn<Locked>());
  EXPECT_EQ(ctx.coins, 0);
  EXPECT_EQ(rec.events.back(), "refused:Locked/Coin/Allowed");
}

TEST(Ctfsm, UnmatchedEventIsIgnored) {
  Ctx ctx;
  Recorder rec;
  Turnstile fsm(&rec);
  fsm.Start(ctx);
  EXPECT_FALSE(fsm.Dispatch(Push{}, ctx));  // no Locked+Push row
  EXPECT_TRUE(fsm.IsIn<Locked>());
  EXPECT_EQ(rec.events.back(), "ignored:Locked/Push");
}

TEST(Ctfsm, WildcardTransitionAppliesFromAnyState) {
  Ctx ctx;
  Turnstile fsm;
  fsm.Start(ctx);
  ASSERT_TRUE(fsm.Dispatch(Coin{}, ctx));   // -> Unlocked
  EXPECT_TRUE(fsm.Dispatch(Reset{}, ctx));  // AnyState + Reset -> Locked
  EXPECT_TRUE(fsm.IsIn<Locked>());
}

TEST(Ctfsm, ObserverSeesTransitionsAndEnters) {
  Ctx ctx;
  Recorder rec;
  Turnstile fsm(&rec);
  fsm.Start(ctx);
  fsm.Dispatch(Coin{}, ctx);
  EXPECT_EQ(rec.events.front(), "enter:Locked");
  EXPECT_EQ(rec.events.back(), "Locked-Coin->Unlocked");
}

// ---- A boot machine to exercise Completion (auto) transitions ----
struct Booting {};
struct Running {};
struct WhenReady {
  bool operator()(const Ctx& c) const noexcept { return c.ready; }
};
using Boot = ctfsm::StateMachine<
    Ctx, ctfsm::StateList<Booting, Running>,
    ctfsm::Table<ctfsm::Row<Booting, ctfsm::Completion, Running, WhenReady>>>;

TEST(Ctfsm, CompletionTransitionFiresOnUpdateWhenGuardPasses) {
  Ctx ctx;
  Boot fsm;
  fsm.Start(ctx);
  fsm.Update(ctx);  // ready == false -> stays
  EXPECT_TRUE(fsm.IsIn<Booting>());
  ctx.ready = true;
  fsm.Update(ctx);  // guard passes -> auto-advance
  EXPECT_TRUE(fsm.IsIn<Running>());
}

}  // namespace
