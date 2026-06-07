// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// The state-machine engine. States are value types held in a preallocated
// tuple; the transition table is types verified at compile time; dispatch is
// allocation-free, RTTI-free, exception-free, and bounded.
#ifndef CTFSM_STATE_MACHINE_HPP_
#define CTFSM_STATE_MACHINE_HPP_

#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ctfsm/observer.hpp"
#include "ctfsm/traits.hpp"
#include "ctfsm/transition_table.hpp"

namespace ctfsm {

template <class Context, class States, class Transitions>
class StateMachine;  // primary template left undefined

template <class Context, class... States, class... Rows>
class StateMachine<Context, StateList<States...>, Table<Rows...>> {
  static_assert(sizeof...(States) > 0,
                "StateList must list at least one state");

  template <class T>
  static constexpr bool is_state_v = (std::is_same_v<T, States> || ...);
  template <class T>
  static constexpr bool is_from_v =
      std::is_same_v<T, AnyState> || is_state_v<T>;

  static_assert((is_from_v<typename Rows::from> && ...),
                "every Row 'From' must be a state in StateList, or AnyState");
  static_assert((is_state_v<typename Rows::to> && ...),
                "every Row 'To' must be a state in StateList");

 public:
  explicit StateMachine(FsmObserver* observer = nullptr) noexcept
      : observer_(observer) {}

  // Enter the initial state (the first in StateList).
  void Start(Context& ctx) noexcept {
    current_ = 0;
    CallOnEnter(ctx);
    if (observer_) observer_->OnEnter(CurrentName());
  }

  // One control tick: run the current state's action, then take an auto
  // (Completion) transition if one is enabled. Silent if none applies.
  void Update(Context& ctx) noexcept {
    CallUpdate(ctx);
    bool refused = false;
    std::string_view guard{};
    (void)(DispatchPass<Completion, true>(ctx, refused, guard) ||
           DispatchPass<Completion, false>(ctx, refused, guard));
  }

  // Process an external event. Returns whether a transition was taken.
  template <class Event>
  bool Dispatch(const Event&, Context& ctx) noexcept {
    bool refused = false;
    std::string_view guard{};
    const bool taken = DispatchPass<Event, true>(ctx, refused, guard) ||
                       DispatchPass<Event, false>(ctx, refused, guard);
    if (!taken && observer_ != nullptr) {
      if (refused) {
        observer_->OnRefused(CurrentName(), name<Event>(), guard);
      } else {
        observer_->OnIgnored(CurrentName(), name<Event>());
      }
    }
    return taken;
  }

  template <class State>
  bool IsIn() const noexcept {
    return current_ == IndexOf<State>();
  }
  std::size_t CurrentId() const noexcept { return current_; }
  std::string_view CurrentName() const noexcept {
    std::string_view n{};
    VisitCurrent([&](const auto& s) { n = name<std::decay_t<decltype(s)>>(); });
    return n;
  }

 private:
  template <class T>
  static constexpr std::size_t IndexOf() noexcept {
    const bool m[] = {std::is_same_v<T, States>...};
    for (std::size_t i = 0; i < sizeof...(States); ++i) {
      if (m[i]) return i;
    }
    return sizeof...(States);
  }

  template <std::size_t I = 0, class F>
  void VisitCurrent(F&& f) noexcept {
    if constexpr (I < sizeof...(States)) {
      if (current_ == I) {
        f(std::get<I>(states_));
        return;
      }
      VisitCurrent<I + 1>(std::forward<F>(f));
    }
  }
  template <std::size_t I = 0, class F>
  void VisitCurrent(F&& f) const noexcept {
    if constexpr (I < sizeof...(States)) {
      if (current_ == I) {
        f(std::get<I>(states_));
        return;
      }
      VisitCurrent<I + 1>(std::forward<F>(f));
    }
  }

  void CallOnEnter(Context& ctx) noexcept {
    VisitCurrent([&](auto& s) {
      using S = std::decay_t<decltype(s)>;
      if constexpr (detail::has_on_enter<S, Context>::value) s.OnEnter(ctx);
    });
  }
  void CallOnExit(Context& ctx) noexcept {
    VisitCurrent([&](auto& s) {
      using S = std::decay_t<decltype(s)>;
      if constexpr (detail::has_on_exit<S, Context>::value) s.OnExit(ctx);
    });
  }
  void CallUpdate(Context& ctx) noexcept {
    VisitCurrent([&](auto& s) {
      using S = std::decay_t<decltype(s)>;
      if constexpr (detail::has_update<S, Context>::value) s.Update(ctx);
    });
  }

  template <class To, class Action, class Event>
  void TransitionTo(Context& ctx) noexcept {
    const std::string_view from = CurrentName();
    CallOnExit(ctx);
    if constexpr (!std::is_same_v<Action, NoAction>) Action{}(ctx);
    current_ = IndexOf<To>();
    CallOnEnter(ctx);
    if (observer_ != nullptr) {
      observer_->OnTransition(from, name<Event>(), name<To>());
    }
  }

  // One pass over the table for `Event`. Exact==true considers concrete-From
  // rows (which must match the current state); Exact==false considers AnyState
  // rows. First row (declaration order) whose guard passes wins.
  template <class Event, bool Exact>
  bool DispatchPass(Context& ctx, bool& refused,
                    std::string_view& guard) noexcept {
    return (TryRow<Event, Exact, Rows>(ctx, refused, guard) || ...);
  }

  template <class Event, bool Exact, class R>
  bool TryRow(Context& ctx, bool& refused, std::string_view& guard) noexcept {
    using From = typename R::from;
    using Ev = typename R::event;
    if constexpr (!std::is_same_v<Ev, Event>) {
      return false;  // event does not match this row
    } else if constexpr (std::is_same_v<From, AnyState> == Exact) {
      return false;  // wrong specificity for this pass
    } else {
      if constexpr (!std::is_same_v<From, AnyState>) {
        if (current_ != IndexOf<From>()) return false;  // not the current state
      }
      if (typename R::guard{}(static_cast<const Context&>(ctx))) {
        TransitionTo<typename R::to, typename R::action, Event>(ctx);
        return true;
      }
      if (!refused) {  // record the first refusing guard
        refused = true;
        guard = name<typename R::guard>();
      }
      return false;
    }
  }

  std::tuple<States...> states_{};
  std::size_t current_{0};
  FsmObserver* observer_{nullptr};
};

}  // namespace ctfsm

#endif  // CTFSM_STATE_MACHINE_HPP_
