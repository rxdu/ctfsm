// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// Proves the engine needs neither exceptions nor RTTI: this translation unit is
// compiled with -fno-exceptions -fno-rtti (see CMakeLists). If any header used
// throw / try / typeid / dynamic_cast, this would fail to compile. It self-
// checks and is registered as a CTest test.
#include <cstdio>

#include "ctfsm/fsm.hpp"

namespace {

struct Ctx {
  int ticks = 0;
};
struct A {
  void Update(Ctx& c) noexcept { ++c.ticks; }
};
struct B {};
struct Go {};

using Fsm = ctfsm::StateMachine<Ctx, ctfsm::StateList<A, B>,
                                ctfsm::Table<ctfsm::Row<A, Go, B>>>;

}  // namespace

int main() {
  Ctx ctx;
  Fsm fsm;
  fsm.Start(ctx);
  fsm.Update(ctx);
  if (!fsm.Dispatch(Go{}, ctx)) {
    std::puts("freestanding: dispatch unexpectedly refused");
    return 1;
  }
  if (!fsm.IsIn<B>()) {
    std::puts("freestanding: wrong state");
    return 1;
  }
  std::puts("freestanding (-fno-exceptions -fno-rtti) OK");
  return 0;
}
