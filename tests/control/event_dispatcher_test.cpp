#include <cstdint>
#include <type_traits>

#include <metl/event_dispatcher.hpp>

namespace {

int free_total = 0;

void capture_free(int value) {
  free_total += value;
}

struct recorder {
  int total;

  void on_event(int value) { total += value; }
  void on_scaled(int value) { total += value * 2; }
};

using dispatcher3 = metl::event_dispatcher<void(int), 3>;

// AUDIT G.5: ids are 64-bit on every target, so a stale id cannot alias a newer
// listener short of 2^64 subscriptions. A size_t id wraps after 2^32 on a 32-bit
// target; there is no hook to start the counter near the wrap, so the width is
// what is pinned.
static_assert(std::is_same<decltype(dispatcher3::listener_id{}.value), std::uint64_t>::value,
              "listener_id must be 64-bit on every target");

// Ids are non-zero and strictly increasing (so distinct) across many
// subscribe/unsubscribe cycles; id 0 is refused; a stale id does not remove the
// listener that reused its slot.
int id_checks() {
  dispatcher3 dispatcher;
  recorder sink{0};
  const auto listener = metl::delegate<void(int)>::bind<recorder, &recorder::on_event>(sink);

  std::uint64_t last = 0;
  for (int i = 0; i < 100000; ++i) {
    auto id = dispatcher.subscribe(listener);
    if (!id || id.value().value == 0 || id.value().value <= last) {
      return 20;
    }
    last = id.value().value;
    if (!dispatcher.unsubscribe(id.value())) {
      return 21;
    }
  }

  auto stale = dispatcher.subscribe(listener);
  if (!stale || !dispatcher.unsubscribe(stale.value())) {
    return 22;
  }
  auto fresh = dispatcher.subscribe(listener);  // reuses the slot stale held
  if (!fresh || dispatcher.unsubscribe(stale.value()) || dispatcher.size() != 1) {
    return 23;
  }

  // Free slots hold id 0; it was never issued, so it must not match one.
  if (dispatcher.unsubscribe(dispatcher3::listener_id{0}) || dispatcher.size() != 1) {
    return 24;
  }
  return 0;
}

}  // namespace

int main() {
  if (const int rc = id_checks()) {
    return rc;
  }

  metl::event_dispatcher<void(int), 3> dispatcher;
  if (!dispatcher.empty() || dispatcher.capacity() != 3) {
    return 1;
  }

  free_total = 0;
  recorder first{0};
  recorder second{0};

  auto free_id = dispatcher.subscribe(metl::delegate<void(int)>::from_function<&capture_free>());
  auto first_id = dispatcher.subscribe(metl::delegate<void(int)>::bind<recorder, &recorder::on_event>(first));
  auto second_id =
      dispatcher.subscribe(metl::delegate<void(int)>::bind<recorder, &recorder::on_scaled>(second));

  if (!free_id || !first_id || !second_id || dispatcher.size() != 3) {
    return 2;
  }

  dispatcher.dispatch(3);
  if (free_total != 3 || first.total != 3 || second.total != 6) {
    return 3;
  }

  if (!dispatcher.unsubscribe(first_id.value()) || dispatcher.size() != 2) {
    return 4;
  }

  dispatcher.dispatch(2);
  if (free_total != 5 || first.total != 3 || second.total != 10) {
    return 5;
  }

  auto reused_id =
      dispatcher.subscribe(metl::delegate<void(int)>::bind<recorder, &recorder::on_event>(first));
  if (!reused_id || dispatcher.size() != 3) {
    return 6;
  }

  if (dispatcher.subscribe(metl::delegate<void(int)>()).has_value()) {
    return 7;
  }

  dispatcher.clear();
  if (!dispatcher.empty()) {
    return 8;
  }

  dispatcher.dispatch(5);
  if (free_total != 5 || first.total != 3 || second.total != 10) {
    return 9;
  }

  return 0;
}
