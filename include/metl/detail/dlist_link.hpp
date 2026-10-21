#pragma once

namespace metl {
namespace detail {

/// @brief One link of a circular doubly linked list: both neighbours, and the
///        list the link belongs to.
///
/// `owner` makes misuse checkable, as in `slist_link` (#337). A list's head is a
/// link too, owned by that list and linked to itself when the list is empty, so
/// `end()` is a position that carries its list like any other. The functions
/// below move `next` and `prev` only; setting `owner` is the list's job.
struct dlist_link {
  dlist_link* next = nullptr;
  dlist_link* prev = nullptr;
  const void* owner = nullptr;
};

/// @brief Links `node` directly before `pos`.
/// @pre `pos` is linked, possibly to itself.
/// Progress guarantee: wait-free, bounded, O(1).
inline void dlist_link_before(dlist_link& pos, dlist_link& node) noexcept {
  node.next = &pos;
  node.prev = pos.prev;
  pos.prev->next = &node;
  pos.prev = &node;
}

/// @brief Unlinks `node` from its neighbours and clears its `next` and `prev`.
/// @pre `node` is linked, and is not a list's head.
/// Progress guarantee: wait-free, bounded, O(1).
inline void dlist_unlink(dlist_link& node) noexcept {
  node.prev->next = node.next;
  node.next->prev = node.prev;
  node.next = nullptr;
  node.prev = nullptr;
}

}  // namespace detail
}  // namespace metl
