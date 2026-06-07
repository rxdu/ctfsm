// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// The "hello world" of state machines: a coin-operated turnstile.
// Doubles as a runnable reference and a CTest test (self-checks, exits non-zero
// on failure regardless of build type — see CHECK below).
#include <iostream>
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

struct Ctx {
  int coins = 0;
};

// States — value types with optional lifecycle hooks.
struct Locked {
  void OnEnter(Ctx&) { std::cout << "  [enter] Locked\n"; }
};
struct Unlocked {
  void OnEnter(Ctx&) { std::cout << "  [enter] Unlocked\n"; }
};

// Events — plain tag types.
struct Coin {};
struct Push {};

// An action: count the coin that unlocked us.
struct CountCoin {
  void operator()(Ctx& c) const noexcept { ++c.coins; }
};

//                  From      Event  To        Guard           Action
using Turnstile = ctfsm::StateMachine<
    Ctx, ctfsm::StateList<Locked, Unlocked>,
    ctfsm::Table<ctfsm::Row<Locked, Coin, Unlocked, ctfsm::Always, CountCoin>,
                 ctfsm::Row<Unlocked, Push, Locked>>>;

}  // namespace

int main() {
  Ctx ctx;
  Turnstile fsm;
  fsm.Start(ctx);  // -> Locked
  CHECK(fsm.IsIn<Locked>());

  std::cout << "insert coin:\n";
  CHECK(fsm.Dispatch(Coin{}, ctx));  // Locked --Coin--> Unlocked
  CHECK(fsm.IsIn<Unlocked>());
  CHECK(ctx.coins == 1);

  std::cout << "push through:\n";
  CHECK(fsm.Dispatch(Push{}, ctx));  // Unlocked --Push--> Locked
  CHECK(fsm.IsIn<Locked>());

  std::cout << "push again while locked (no such transition -> ignored):\n";
  CHECK(!fsm.Dispatch(Push{}, ctx));
  CHECK(fsm.IsIn<Locked>());

  std::cout << "turnstile OK\n";
  return 0;
}
