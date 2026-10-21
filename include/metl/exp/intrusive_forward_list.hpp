#pragma once

/// @file
/// @brief `metl::exp::intrusive_forward_list<T, Tag>`: a singly linked list of
///        objects the caller owns, through a hook that checks its own use.
///
/// experimental: may change or be removed in a minor release.
/// Tracking issue: #337, which states when this is promoted and when it is
/// removed. It is not included by <metl/metl.hpp>, and the 1.x Semantic
/// Versioning promise does not cover it (docs/SCOPE.md section 3, Tier 2).

#include "metl/config.hpp"
#include "metl/detail/slist_link.hpp"

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace metl {
namespace exp {

template <typename T, typename Tag>
class intrusive_forward_list;

/// @brief The base an element derives from to be linked into an
///        `intrusive_forward_list<T, Tag>`.
///
/// An element derives from one hook per list it can be in at the same time,
/// each with its own `Tag`:
///
/// @code
///   struct by_deadline {};
///   struct timer : metl::exp::forward_list_hook<>,             // the all-timers list
///                  metl::exp::forward_list_hook<by_deadline> {  // the armed list
///     ...
///   };
/// @endcode
///
/// The hook records its successor and the list it is in: two pointers, one
/// more than an unchecked hook. That second pointer is the check: METL_HARDEN,
/// on at every hardening level, stops the program when an element is
///   * destroyed while it is linked, which would leave its list pointing at a
///     dead object;
///   * linked while it is already linked, into another list or the same one;
///   * erased through a list it is not in.
///
/// What no hook can check: storage reused or freed without the element's
/// destructor running (a `memset`, a placement new over it, a pool that drops
/// it). Its list then points at whatever the storage holds next. Run the
/// destructor, or unlink the element first.
///
/// Copying an element does not copy its place in a list: a copy-constructed
/// hook is unlinked, and assignment leaves the target's hook as it was.
///
/// @tparam Tag Names the list; `void` when an element is in one list only.
template <typename Tag = void>
class forward_list_hook : private metl::detail::slist_link {
 public:
  constexpr forward_list_hook() noexcept = default;
  constexpr forward_list_hook(const forward_list_hook&) noexcept : metl::detail::slist_link() {}
  constexpr forward_list_hook(forward_list_hook&&) noexcept : metl::detail::slist_link() {}
  // Assignment keeps this hook's own link and copies nothing from `other`, so
  // assigning a hook to itself is a no-op like any other assignment.
  // NOLINTNEXTLINE(cert-oop54-cpp)
  constexpr forward_list_hook& operator=(const forward_list_hook&) noexcept { return *this; }
  constexpr forward_list_hook& operator=(forward_list_hook&&) noexcept { return *this; }

  ~forward_list_hook() { METL_HARDEN(owner == nullptr); }  // destroyed while linked

  /// @brief Whether the element is in a list through this hook.
  constexpr bool is_linked() const noexcept { return owner != nullptr; }

 private:
  template <typename, typename>
  friend class intrusive_forward_list;
};

/// @brief A singly linked list of `T` objects the caller owns, linked through
///        their `forward_list_hook<Tag>` base.
///
/// The list stores no element. It links the caller's objects, wherever they
/// live: a static descriptor table, a member of a driver's state, a stack frame.
/// That is the case `handle_pool` cannot serve, since it stores its objects
/// itself. Linking and unlinking never allocate, copy or construct an element.
///
/// The interface follows `std::forward_list` where the operation exists, but
/// elements are passed by reference and are linked, not copied:
///   * `push_front(t)`, `pop_front()`, `front()`;
///   * `insert_after(pos, t)`, `erase_after(pos)` at a position;
///   * `erase(t)`: unlinks `t` wherever it is, O(n);
///   * `clear()`, `empty()`, `size()` (O(1)), forward iterators.
/// Unlinking an element leaves it alive; the list never destroys one. The
/// list's own destructor unlinks every element, so they may outlive it.
///
/// The list is neither copyable nor movable: each element records its list's
/// address, so the list stays where it was built. Its constructor is
/// `constexpr`, so a namespace-scope list is constant-initialized.
///
/// Iterators and references stay valid until their element is unlinked.
///
/// Progress guarantees:
///
///   | Operation                                     | Guarantee                |
///   |-----------------------------------------------|--------------------------|
///   | `push_front` / `pop_front` / `front`          | wait-free, bounded, O(1) |
///   | `insert_after` / `erase_after`                | wait-free, bounded, O(1) |
///   | `empty` / `size` / iteration step             | wait-free, bounded, O(1) |
///   | `erase(t)`                                    | wait-free, bounded, O(n) |
///   | `clear` / destructor                          | wait-free, bounded, O(n) |
///
/// @note Not thread-safe, like the other containers: an ISR that links or
///       unlinks while a thread does needs a lock around both.
///
/// @tparam T   The element type; derives publicly from `forward_list_hook<Tag>`.
/// @tparam Tag Which of `T`'s hooks this list uses.
template <typename T, typename Tag = void>
class intrusive_forward_list {
  using link = metl::detail::slist_link;

 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using hook_type = forward_list_hook<Tag>;

  template <bool Const>
  class basic_iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<Const, const T*, T*>;
    using reference = std::conditional_t<Const, const T&, T&>;

    constexpr basic_iterator() noexcept = default;
    // iterator -> const_iterator, implicit as for every standard container.
    template <bool C = Const, typename = std::enable_if_t<C>>
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,google-explicit-constructor)
    constexpr basic_iterator(const basic_iterator<false>& other) noexcept : node_(other.node_) {}

    reference operator*() const noexcept { return value_of(*node_); }
    pointer operator->() const noexcept { return &value_of(*node_); }
    basic_iterator& operator++() noexcept {
      node_ = node_->next;
      return *this;
    }
    basic_iterator operator++(int) noexcept {
      basic_iterator old = *this;
      node_ = node_->next;
      return old;
    }
    friend bool operator==(const basic_iterator& a, const basic_iterator& b) noexcept {
      return a.node_ == b.node_;
    }
    friend bool operator!=(const basic_iterator& a, const basic_iterator& b) noexcept {
      return a.node_ != b.node_;
    }

   private:
    friend class intrusive_forward_list;
    friend class basic_iterator<!Const>;
    constexpr explicit basic_iterator(link* node) noexcept : node_(node) {}
    link* node_ = nullptr;
  };
  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

  constexpr intrusive_forward_list() noexcept : head_{nullptr, this} {
    static_assert(std::is_base_of_v<hook_type, T>,
                  "intrusive_forward_list<T, Tag>: T must derive publicly from forward_list_hook<Tag>");
  }
  ~intrusive_forward_list() { clear(); }

  intrusive_forward_list(const intrusive_forward_list&) = delete;
  intrusive_forward_list& operator=(const intrusive_forward_list&) = delete;
  intrusive_forward_list(intrusive_forward_list&&) = delete;
  intrusive_forward_list& operator=(intrusive_forward_list&&) = delete;

  // ---- Observers -----------------------------------------------------------

  bool empty() const noexcept { return head_.next == nullptr; }
  size_type size() const noexcept { return size_; }

  /// @pre `!empty()`.
  T& front() noexcept {
    METL_ASSERT(!empty());
    return value_of(*head_.next);
  }
  /// @pre `!empty()`.
  const T& front() const noexcept {
    METL_ASSERT(!empty());
    return value_of(*head_.next);
  }

  // ---- Iterators -----------------------------------------------------------

  /// @brief The position before the first element, for `insert_after` and
  ///        `erase_after`. Not dereferenceable.
  iterator before_begin() noexcept { return iterator(&head_); }
  const_iterator before_begin() const noexcept { return const_iterator(&head_); }
  const_iterator cbefore_begin() const noexcept { return before_begin(); }
  iterator begin() noexcept { return iterator(head_.next); }
  const_iterator begin() const noexcept { return const_iterator(head_.next); }
  const_iterator cbegin() const noexcept { return begin(); }
  iterator end() noexcept { return iterator(); }
  const_iterator end() const noexcept { return const_iterator(); }
  const_iterator cend() const noexcept { return end(); }

  // ---- Modifiers -----------------------------------------------------------

  /// @brief Links `value` first.
  /// @pre `value` is in no list through this hook.
  void push_front(T& value) noexcept { insert_after(before_begin(), value); }

  /// @brief Links `value` directly after `pos`.
  /// @pre `pos` is `before_begin()` or an element of this list, and `value` is
  ///      in no list through this hook.
  /// @return An iterator to `value`.
  iterator insert_after(const_iterator pos, T& value) noexcept {
    link& node = link_of(value);
    METL_HARDEN(node.owner == nullptr);  // already linked
    METL_HARDEN(in_this_list(pos));      // a position in another list, or end()
    metl::detail::slist_link_after(*pos.node_, node);
    node.owner = this;
    ++size_;
    return iterator(&node);
  }

  /// @brief Unlinks the first element.
  /// @pre `!empty()`.
  void pop_front() noexcept { (void)erase_after(before_begin()); }

  /// @brief Unlinks the element after `pos`.
  /// @pre `pos` is `before_begin()` or an element of this list, and is not the
  ///      last.
  /// @return An iterator to the element after the unlinked one, or `end()`.
  iterator erase_after(const_iterator pos) noexcept {
    METL_HARDEN(in_this_list(pos));           // a position in another list, or end()
    METL_HARDEN(pos.node_->next != nullptr);  // nothing after pos
    unlink_after(*pos.node_);
    return iterator(pos.node_->next);
  }

  /// @brief Unlinks `value`, wherever it is in this list. O(n).
  /// @pre `value` is in this list.
  void erase(T& value) noexcept {
    link& node = link_of(value);
    METL_HARDEN(node.owner == this);  // in another list, or in none
    unlink_after(metl::detail::slist_previous(head_, node));
  }

  /// @brief Unlinks every element.
  void clear() noexcept {
    while (head_.next != nullptr) {
      unlink_after(head_);
    }
  }

 private:
  static link& link_of(T& value) noexcept { return static_cast<link&>(static_cast<hook_type&>(value)); }
  static T& value_of(link& node) noexcept { return static_cast<T&>(static_cast<hook_type&>(node)); }

  bool in_this_list(const_iterator pos) const noexcept {
    return pos.node_ != nullptr && pos.node_->owner == this;
  }

  void unlink_after(link& pos) noexcept {
    link& node = metl::detail::slist_unlink_after(pos);
    node.owner = nullptr;
    --size_;
  }

  // Mutable because a position is a `link*` whatever its constness, and a
  // const list's before_begin() is one. No const member writes through it: only
  // the non-const modifiers take a position, and they check it is in this list.
  mutable link head_;
  size_type size_ = 0;
};

}  // namespace exp
}  // namespace metl
