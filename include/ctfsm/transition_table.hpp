// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// The declarative transition table: states, events, guards, actions, and rows.
#ifndef CTFSM_TRANSITION_TABLE_HPP_
#define CTFSM_TRANSITION_TABLE_HPP_

#include <string_view>

namespace ctfsm {

// The set of states. The first listed state is the initial state.
template <class... States>
struct StateList {};

// Wildcard `From`: a row whose source is AnyState applies in every state
// (e.g. an E-stop transition reachable from anywhere).
struct AnyState {};

// The implicit event used for automatic ("completion") transitions, evaluated
// each Update() rather than dispatched externally.
struct Completion {};

// Default guard: the transition is always enabled.
struct Always {
  static constexpr std::string_view kName = "Always";
  template <class Ctx>
  constexpr bool operator()(const Ctx&) const noexcept {
    return true;
  }
};

// Default action: no side effect on transition.
struct NoAction {
  template <class Ctx>
  constexpr void operator()(Ctx&) const noexcept {}
};

// One legal transition. Guards/actions are default-constructible callable types
// (functor structs; C++17 captureless lambdas are not default-constructible).
//   Guard:  bool(const Context&)
//   Action: void(Context&)
template <class From, class Event, class To, class Guard = Always,
          class Action = NoAction>
struct Row {
  using from = From;
  using event = Event;
  using to = To;
  using guard = Guard;
  using action = Action;
};

// The transition table — the single, enforced source of truth for the graph.
template <class... Rows>
struct Table {};

}  // namespace ctfsm

#endif  // CTFSM_TRANSITION_TABLE_HPP_
