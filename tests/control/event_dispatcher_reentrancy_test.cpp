// event_dispatcher: subscribe and unsubscribe from inside a listener.
//
// dispatch() calls the listeners subscribed when the event began, and only
// those still subscribed when their turn comes. Before this was stated, whether
// a listener subscribed mid-dispatch heard the current event depended on which
// slot it landed in: a later slot was called, an earlier free one was not.
#include "metl_check.hpp"

#include <metl/event_dispatcher.hpp>

namespace {

using dispatcher_t = metl::event_dispatcher<void(int), 4>;
using delegate_t = dispatcher_t::delegate_type;

dispatcher_t* g_dispatcher = nullptr;
int g_newcomer_events = 0;
int g_newcomer_last = 0;
int g_victim_events = 0;
dispatcher_t::listener_id g_victim_id{0};
bool g_subscribed = false;

void newcomer(int value) {
  ++g_newcomer_events;
  g_newcomer_last = value;
}

void victim(int) {
  ++g_victim_events;
}

void filler(int) {}

// Subscribes `newcomer` the first time it runs.
void subscriber(int) {
  if (!g_subscribed) {
    g_subscribed = true;
    CHECK(g_dispatcher->subscribe(delegate_t::from_function<&newcomer>()).has_value());
  }
}

// Subscribes `newcomer`, then raises a second event from inside the first.
void nested_subscriber(int value) {
  if (!g_subscribed) {
    g_subscribed = true;
    CHECK(g_dispatcher->subscribe(delegate_t::from_function<&newcomer>()).has_value());
    g_dispatcher->dispatch(value + 100);
  }
}

void unsubscriber(int) {
  (void)g_dispatcher->unsubscribe(g_victim_id);
}

void reset(dispatcher_t& dispatcher) {
  dispatcher.clear();
  g_dispatcher = &dispatcher;
  g_newcomer_events = 0;
  g_newcomer_last = 0;
  g_victim_events = 0;
  g_subscribed = false;
}

}  // namespace

int main() {
  // Static, not automatic: the listeners reach it through g_dispatcher, and a
  // global pointing at a local is what GCC's -Wdangling-pointer reports.
  static dispatcher_t dispatcher;

  // The newcomer lands in a LATER slot than the listener that subscribed it.
  // It used to be called for the current event; now it is not.
  {
    reset(dispatcher);
    CHECK(dispatcher.subscribe(delegate_t::from_function<&subscriber>()).has_value());  // slot 0
    dispatcher.dispatch(1);                                                             // newcomer -> slot 1
    CHECK_EQ(g_newcomer_events, 0);
    dispatcher.dispatch(2);
    CHECK_EQ(g_newcomer_events, 1);
    CHECK_EQ(g_newcomer_last, 2);
  }

  // The newcomer lands in an EARLIER free slot. It was never called for the
  // current event, and still is not: both placements now agree.
  {
    reset(dispatcher);
    const auto hole = dispatcher.subscribe(delegate_t::from_function<&filler>());       // slot 0
    CHECK(dispatcher.subscribe(delegate_t::from_function<&subscriber>()).has_value());  // slot 1
    CHECK(dispatcher.unsubscribe(hole.value()));                                        // slot 0 free
    dispatcher.dispatch(1);                                                             // newcomer -> slot 0
    CHECK_EQ(g_newcomer_events, 0);
    dispatcher.dispatch(2);
    CHECK_EQ(g_newcomer_events, 1);
  }

  // Unsubscribing a listener whose turn has not come: it is not called.
  {
    reset(dispatcher);
    CHECK(dispatcher.subscribe(delegate_t::from_function<&unsubscriber>()).has_value());  // slot 0
    g_victim_id = dispatcher.subscribe(delegate_t::from_function<&victim>()).value();     // slot 1
    dispatcher.dispatch(1);
    CHECK_EQ(g_victim_events, 0);
    CHECK_EQ(dispatcher.size(), std::size_t{1});
  }

  // A victim's slot reused mid-dispatch by a newcomer: neither is called. The
  // victim is gone; the newcomer arrived after the event began.
  {
    reset(dispatcher);
    CHECK(dispatcher.subscribe(delegate_t::from_function<&unsubscriber>()).has_value());  // slot 0
    CHECK(dispatcher.subscribe(delegate_t::from_function<&subscriber>()).has_value());    // slot 1
    g_victim_id = dispatcher.subscribe(delegate_t::from_function<&victim>()).value();     // slot 2
    // slot 0 frees slot 2; slot 1 then subscribes the newcomer, which takes slot 2.
    dispatcher.dispatch(1);
    CHECK_EQ(g_victim_events, 0);
    CHECK_EQ(g_newcomer_events, 0);
  }

  // An event raised from inside a listener is a new event: a listener
  // subscribed before it began hears it, and not the outer event.
  {
    reset(dispatcher);
    CHECK(dispatcher.subscribe(delegate_t::from_function<&nested_subscriber>()).has_value());  // slot 0
    dispatcher.dispatch(1);  // newcomer -> slot 1, then dispatch(101) from inside
    CHECK_EQ(g_newcomer_events, 1);
    CHECK_EQ(g_newcomer_last, 101);
  }

  return metl_test::exit_code();
}
