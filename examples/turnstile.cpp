// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// The "hello world" of state machines: a coin-operated turnstile.
//
// Recommended structure: wrap the machine in a small class that OWNS the FSM as
// a private member and exposes a domain API. Callers see intent (InsertCoin /
// PushThrough), not transitions; the context, states, events, action, and table
// are private implementation detail grouped in one place. Doubles as a CTest
// test (self-checks; exits non-zero on failure regardless of build type).
#include <iostream>

#include "ctfsm/fsm.hpp"

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "CHECK failed: " #cond " @line " << __LINE__ << "\n"; \
      return 1;                                                          \
    }                                                                    \
  } while (0)

namespace {

class Turnstile {
 public:
  Turnstile() { fsm_.Start(ctx_); }

  // Domain API — what the turnstile does, not how it transitions.
  void InsertCoin() { fsm_.Dispatch(Coin{}, ctx_); }
  void PushThrough() { fsm_.Dispatch(Push{}, ctx_); }

  bool locked() const { return fsm_.IsIn<Locked>(); }
  int coins() const { return ctx_.coins; }

 private:
  // --- everything the machine is made of, grouped here ---
  struct Ctx {
    int coins = 0;
  };

  struct Locked {
    void OnEnter(Ctx&) { std::cout << "  [enter] Locked\n"; }
  };
  struct Unlocked {
    void OnEnter(Ctx&) { std::cout << "  [enter] Unlocked\n"; }
  };

  struct Coin {};
  struct Push {};

  struct CountCoin {
    void operator()(Ctx& c) const noexcept { ++c.coins; }
  };

  //                       From    Event  To        Guard          Action
  using Fsm = ctfsm::StateMachine<
      Ctx, ctfsm::StateList<Locked, Unlocked>,
      ctfsm::Table<ctfsm::Row<Locked, Coin, Unlocked, ctfsm::Always, CountCoin>,
                   ctfsm::Row<Unlocked, Push, Locked>>>;

  Ctx ctx_{};
  Fsm fsm_{};
};

}  // namespace

int main() {
  Turnstile t;
  CHECK(t.locked());

  std::cout << "insert coin:\n";
  t.InsertCoin();
  CHECK(!t.locked());
  CHECK(t.coins() == 1);

  std::cout << "push through:\n";
  t.PushThrough();
  CHECK(t.locked());

  std::cout << "push again while locked (no such transition -> ignored):\n";
  t.PushThrough();
  CHECK(t.locked());

  std::cout << "turnstile OK\n";
  return 0;
}
