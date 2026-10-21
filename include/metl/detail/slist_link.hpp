#pragma once

namespace metl {
namespace detail {

/// @brief One link of a singly linked list: the successor, and the list the
///        link belongs to.
///
/// `owner` is what makes misuse checkable (#337): a link with no owner is in no
/// list, and an operation handed a link or position from another list can tell
/// by comparing it with its own address. A list's head is a link too, owned by
/// that list, so a position before the first element carries its list as well.
/// The functions below move `next` only; setting `owner` is the list's job.
struct slist_link {
  slist_link* next = nullptr;
  const void* owner = nullptr;
};

/// @brief Links `node` directly after `pos`.
/// Progress guarantee: wait-free, bounded, O(1).
inline void slist_link_after(slist_link& pos, slist_link& node) noexcept {
  node.next = pos.next;
  pos.next = &node;
}

/// @brief Unlinks the link after `pos` and returns it, its `next` cleared.
/// @pre `pos.next != nullptr`.
/// Progress guarantee: wait-free, bounded, O(1).
inline slist_link& slist_unlink_after(slist_link& pos) noexcept {
  slist_link& node = *pos.next;
  pos.next = node.next;
  node.next = nullptr;
  return node;
}

/// @brief The link whose successor is `node`, walking from `head`.
/// @pre `node` is reachable from `head`.
/// Progress guarantee: wait-free, bounded, O(n) in the links before `node`.
inline slist_link& slist_previous(slist_link& head, const slist_link& node) noexcept {
  slist_link* pos = &head;
  while (pos->next != &node) {
    pos = pos->next;
  }
  return *pos;
}

}  // namespace detail
}  // namespace metl
