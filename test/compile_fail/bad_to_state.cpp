// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// Negative compile test: this table MUST fail to compile (CMake WILL_FAIL).
#include "ctfsm/fsm.hpp"
namespace {
struct Ctx {};
struct A {};
struct B {};
struct NotAState {};
struct E {};
}  // namespace
using Bad = ctfsm::StateMachine<Ctx, ctfsm::StateList<A, B>,
                                ctfsm::Table<ctfsm::Row<A, E, NotAState>>>;
Bad make();  // odr-use to force instantiation
int main() {
  (void)sizeof(Bad);
  return 0;
}
