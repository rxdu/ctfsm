#include "ctfsm/fsm.hpp"
namespace {
struct Ctx {};
struct A {};
struct B {};
struct C {};
struct E {};
}  // namespace
using Bad =
    ctfsm::StateMachine<Ctx, ctfsm::StateList<A, B, C>,
                        ctfsm::Table<ctfsm::Row<A, E, B>>>;  // C unreachable
int main() {
  (void)sizeof(Bad);
  return 0;
}
