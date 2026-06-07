// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// Real-time-suitability tests. The library claims an allocation-free, noexcept,
// vtable-free, deterministic, bounded hot path — these tests verify each claim
// rather than taking it on faith.
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include "ctfsm/fsm.hpp"

// --- Count every global heap allocation in this test binary (forward to
//     malloc/free). The RT tests bracket the engine's hot path and assert the
//     count does not move. ---
namespace {
std::atomic<long> g_allocations{0};
}  // namespace

void* operator new(std::size_t n) {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  void* p = std::malloc(n != 0 ? n : 1);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}
void* operator new[](std::size_t n) {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  void* p = std::malloc(n != 0 ? n : 1);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

struct Ctx {
  int ticks = 0;
};

struct A {
  void OnEnter(Ctx&) noexcept {}
  void Update(Ctx& c) noexcept { ++c.ticks; }
};
struct B {
  void OnEnter(Ctx&) noexcept {}
};
struct Go {};
struct Back {};
struct GuardOk {
  bool operator()(const Ctx&) const noexcept { return true; }
};

using Fsm = ctfsm::StateMachine<
    Ctx, ctfsm::StateList<A, B>,
    ctfsm::Table<ctfsm::Row<A, Go, B, GuardOk>, ctfsm::Row<B, Back, A>>>;

// ---- Compile-time real-time properties ----
static_assert(
    !std::is_polymorphic_v<Fsm>,
    "the engine must carry no vtable (value-type states, no virtual)");
static_assert(std::is_nothrow_default_constructible_v<Fsm>,
              "constructing the machine must not throw");
static_assert(std::is_trivially_destructible_v<Fsm>,
              "with trivial states the machine has no dynamic teardown");
static_assert(noexcept(std::declval<Fsm&>().Start(std::declval<Ctx&>())),
              "Start must be noexcept");
static_assert(noexcept(std::declval<Fsm&>().Update(std::declval<Ctx&>())),
              "Update must be noexcept");
static_assert(noexcept(std::declval<Fsm&>().Dispatch(std::declval<Go>(),
                                                     std::declval<Ctx&>())),
              "Dispatch must be noexcept");
static_assert(noexcept(std::declval<const Fsm&>().CurrentName()),
              "CurrentName must be noexcept");

// The machine is small and fixed-size (no per-transition growth).
static_assert(sizeof(Fsm) <= 4 * sizeof(void*),
              "machine footprint should be a few words, not a container");

TEST(Realtime, StartAllocatesNothing) {
  Ctx ctx;
  Fsm fsm;
  const long before = g_allocations.load();
  fsm.Start(ctx);
  EXPECT_EQ(g_allocations.load(), before);
}

TEST(Realtime, HotPathAllocatesNothingOverManyTicks) {
  Ctx ctx;
  Fsm fsm;
  fsm.Start(ctx);
  const long before = g_allocations.load();
  for (int i = 0; i < 100000; ++i) {
    fsm.Update(ctx);            // current-state action + completion check
    fsm.Dispatch(Go{}, ctx);    // A -> B (guarded)
    fsm.Dispatch(Back{}, ctx);  // B -> A
    fsm.Dispatch(Back{}, ctx);  // ignored from A (no matching row)
  }
  EXPECT_EQ(g_allocations.load(), before) << "engine allocated on the hot path";
}

TEST(Realtime, ObserverPathAllocatesNothing) {
  struct NoopObserver : ctfsm::FsmObserver {};  // all hooks default to no-op
  NoopObserver obs;
  Ctx ctx;
  Fsm fsm(&obs);
  fsm.Start(ctx);
  const long before = g_allocations.load();
  for (int i = 0; i < 10000; ++i) {
    fsm.Dispatch(Go{}, ctx);
    fsm.Dispatch(Back{}, ctx);
  }
  EXPECT_EQ(g_allocations.load(), before)
      << "transition/observer notification allocated";
}

TEST(Realtime, DeterministicTrajectory) {
  const auto run = [] {
    Ctx ctx;
    Fsm fsm;
    fsm.Start(ctx);
    std::vector<std::size_t> traj;
    for (int i = 0; i < 50; ++i) {
      fsm.Dispatch(Go{}, ctx);
      traj.push_back(fsm.CurrentId());
      fsm.Dispatch(Back{}, ctx);
      traj.push_back(fsm.CurrentId());
    }
    return traj;
  };
  EXPECT_EQ(run(), run());  // same inputs -> identical state trajectory
}

}  // namespace
