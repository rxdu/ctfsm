// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// Negative compile test: this table MUST fail to compile (CMake WILL_FAIL).
#include "ctfsm/fsm.hpp"
namespace {
struct Ctx {};
struct A {};
struct B {};
struct C {};
struct E {};
}  // namespace
using Bad = ctfsm::StateMachine<
    Ctx, ctfsm::StateList<A, B, C>,
    ctfsm::Table<ctfsm::Row<A, E, B>, ctfsm::Row<A, E, C>>>;  // both Always
int main() {
  (void)sizeof(Bad);
  return 0;
}
