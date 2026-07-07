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

}  // namespace

int main() {
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
