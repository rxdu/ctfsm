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
