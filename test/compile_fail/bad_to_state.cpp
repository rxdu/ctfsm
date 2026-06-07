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
