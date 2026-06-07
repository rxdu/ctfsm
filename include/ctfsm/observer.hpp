// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// Observer for traceability. The engine notifies it ONLY on transitions/events
// (never per-tick), so it is off the real-time-critical path. Wire it to a
// trace sink that records non-blocking.
#ifndef CTFSM_OBSERVER_HPP_
#define CTFSM_OBSERVER_HPP_

#include <string_view>

namespace ctfsm {

struct FsmObserver {
  virtual void OnEnter(std::string_view state) noexcept { (void)state; }
  virtual void OnExit(std::string_view state) noexcept { (void)state; }
  virtual void OnTransition(std::string_view from, std::string_view event,
                            std::string_view to) noexcept {
    (void)from;
    (void)event;
    (void)to;
  }
  // A matching transition existed but its guard refused; `guard` names it.
  virtual void OnRefused(std::string_view from, std::string_view event,
                         std::string_view guard) noexcept {
    (void)from;
    (void)event;
    (void)guard;
  }
  // No transition in the table matched (state, event).
  virtual void OnIgnored(std::string_view from,
                         std::string_view event) noexcept {
    (void)from;
    (void)event;
  }
  virtual ~FsmObserver() = default;
};

}  // namespace ctfsm

#endif  // CTFSM_OBSERVER_HPP_
