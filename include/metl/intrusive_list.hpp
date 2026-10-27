#pragma once

/// @file
/// @brief `metl::intrusive_list<T, Tag>`: a doubly linked list of objects
///        the caller owns, through a hook that checks its own use.

#include "metl/attributes.hpp"
#include "metl/config.hpp"
#include "metl/detail/dlist_link.hpp"

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace metl {

template <typename T, typename Tag>
class intrusive_list;

/// @brief The base an element derives from to be linked into an
///        `intrusive_list<T, Tag>`.
///
/// An element derives from one hook per list it can be in at the same time,
/// each with its own `Tag`:
///
/// @code
///   struct all_timers {};
///   struct armed {};
///   struct timer : metl::list_hook<all_timers>,  // every timer
///                  metl::list_hook<armed> {      // the ones counting down
///     ...
///   };
/// @endcode
///
/// The hook records both neighbours and the list it is in: three pointers, one
/// more than an unchecked hook. That third pointer is the check: METL_HARDEN,
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
/// An element with static storage duration linked into a list in another
/// translation unit is destroyed at exit in an order the program does not
/// choose, possibly before that list unlinks it, and the first check then
/// stops the program. Whatever links such an element unlinks it in its own
/// destructor, if `is_linked()`; the list's destructor has unlinked it
/// otherwise. `examples/driver_registry.cpp` shows the pattern.
///
/// Copying an element does not copy its place in a list: a copy-constructed
/// hook is unlinked, and assignment leaves the target's hook as it was.
///
/// @tparam Tag Names the list; `void` when an element is in one list only.
template <typename Tag = void>
class list_hook : private metl::detail::dlist_link {
 public:
  constexpr list_hook() noexcept = default;
  constexpr list_hook(const list_hook&) noexcept : metl::detail::dlist_link() {}
  constexpr list_hook(list_hook&&) noexcept : metl::detail::dlist_link() {}
  // Assignment keeps this hook's own links and copies nothing from `other`, so
  // assigning a hook to itself is a no-op like any other assignment.
  // NOLINTNEXTLINE(cert-oop54-cpp)
  constexpr list_hook& operator=(const list_hook&) noexcept { return *this; }
  constexpr list_hook& operator=(list_hook&&) noexcept { return *this; }

  ~list_hook() { METL_HARDEN(owner == nullptr); }  // destroyed while linked

  /// @brief Whether the element is in a list through this hook.
  METL_NODISCARD constexpr bool is_linked() const noexcept { return owner != nullptr; }

 private:
  template <typename, typename>
  friend class intrusive_list;
};

/// @brief A doubly linked list of `T` objects the caller owns, linked through
///        their `list_hook<Tag>` base.
///
/// The list stores no element. It links the caller's objects, wherever they
/// live, and never allocates, copies or constructs one. Over
/// `intrusive_forward_list` it adds unlinking any element in O(1), as a
/// cancelled timer needs, for one more pointer per hook
/// (`examples/armed_timers.cpp`).
///
/// The interface follows `std::list` where the operation exists, but elements
/// are passed by reference and are linked, not copied:
///   * `push_front(t)`, `push_back(t)`, `pop_front()`, `pop_back()`,
///     `front()`, `back()`;
///   * `insert(pos, t)`, linking `t` before `pos`; `erase(pos)`;
///   * `erase(t)`: unlinks `t` wherever it is;
///   * `splice(pos, other, it)`: moves one element, from `other` or from this
///     list, to before `pos`, without unlinking it;
///   * `clear()`, `empty()`, `size()`, bidirectional iterators.
/// Unlinking an element leaves it alive; the list never destroys one. The
/// list's own destructor unlinks every element, so they may outlive it.
///
/// The list is neither copyable nor movable: its head and each element record
/// its address, so the list stays where it was built. Its constructor is
/// `constexpr`, so a namespace-scope list is constant-initialized.
///
/// Iterators and references stay valid until their element is unlinked.
///
/// Progress guarantees:
///
///   | Operation                                     | Guarantee                              |
///   |-----------------------------------------------|----------------------------------------|
///   | `push_*` / `pop_*` / `front` / `back`         | wait-free, bounded, O(1)               |
///   | `insert` / `erase(pos)` / `erase(t)`          | wait-free, bounded, O(1)               |
///   | `splice`                                      | wait-free, bounded, O(1)               |
///   | `empty` / `size` / iteration step             | wait-free, bounded, O(1)               |
///   | `clear` / destructor                          | wait-free, bounded by the caller, O(n) |
///
/// The n of `clear` is the number of elements linked. No capacity bounds it:
/// the caller does, by the objects it links (docs/SCOPE.md section 1, "bounded
/// by the caller").
///
/// @note Not thread-safe, like the other containers: an ISR that links or
///       unlinks while a thread does needs a lock around both.
///
/// @tparam T   The element type; derives publicly from `list_hook<Tag>`.
/// @tparam Tag Which of `T`'s hooks this list uses.
template <typename T, typename Tag = void>
class intrusive_list {
  using link = metl::detail::dlist_link;

 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using hook_type = list_hook<Tag>;

  template <bool Const>
  class basic_iterator {
   public:
    using iterator_category = std::bidirectional_iterator_tag;
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
    basic_iterator& operator--() noexcept {
      node_ = node_->prev;
      return *this;
    }
    basic_iterator operator--(int) noexcept {
      basic_iterator old = *this;
      node_ = node_->prev;
      return old;
    }
    friend bool operator==(const basic_iterator& a, const basic_iterator& b) noexcept {
      return a.node_ == b.node_;
    }
    friend bool operator!=(const basic_iterator& a, const basic_iterator& b) noexcept {
      return a.node_ != b.node_;
    }

   private:
    friend class intrusive_list;
    friend class basic_iterator<!Const>;
    constexpr explicit basic_iterator(link* node) noexcept : node_(node) {}
    link* node_ = nullptr;
  };
  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

  constexpr intrusive_list() noexcept : head_{&head_, &head_, this} {
    static_assert(std::is_base_of_v<hook_type, T>,
                  "intrusive_list<T, Tag>: T must derive publicly from list_hook<Tag>");
  }
  ~intrusive_list() { clear(); }

  intrusive_list(const intrusive_list&) = delete;
  intrusive_list& operator=(const intrusive_list&) = delete;
  intrusive_list(intrusive_list&&) = delete;
  intrusive_list& operator=(intrusive_list&&) = delete;

  // ---- Observers -----------------------------------------------------------

  METL_NODISCARD bool empty() const noexcept { return head_.next == &head_; }
  METL_NODISCARD size_type size() const noexcept { return size_; }

  /// @pre `!empty()`.
  METL_NODISCARD T& front() noexcept {
    METL_ASSERT(!empty());
    return value_of(*head_.next);
  }
  /// @pre `!empty()`.
  METL_NODISCARD const T& front() const noexcept {
    METL_ASSERT(!empty());
    return value_of(*head_.next);
  }
  /// @pre `!empty()`.
  METL_NODISCARD T& back() noexcept {
    METL_ASSERT(!empty());
    return value_of(*head_.prev);
  }
  /// @pre `!empty()`.
  METL_NODISCARD const T& back() const noexcept {
    METL_ASSERT(!empty());
    return value_of(*head_.prev);
  }

  // ---- Iterators -----------------------------------------------------------

  METL_NODISCARD iterator begin() noexcept { return iterator(head_.next); }
  METL_NODISCARD const_iterator begin() const noexcept { return const_iterator(head_.next); }
  METL_NODISCARD const_iterator cbegin() const noexcept { return begin(); }
  /// @brief The head: a position in this list, before which `insert` appends.
  ///        Not dereferenceable.
  METL_NODISCARD iterator end() noexcept { return iterator(&head_); }
  METL_NODISCARD const_iterator end() const noexcept {
    // A position is a `link*` whatever its constness, as insert and erase
    // take a const_iterator and write through it. Only those non-const
    // members write through a position, after checking it is in this list,
    // so nothing writes to a const list's head.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_iterator(const_cast<link*>(&head_));
  }
  METL_NODISCARD const_iterator cend() const noexcept { return end(); }

  // ---- Modifiers -----------------------------------------------------------

  /// @brief Links `value` first.
  /// @pre `value` is in no list through this hook.
  void push_front(T& value) noexcept { (void)insert(begin(), value); }

  /// @brief Links `value` last.
  /// @pre `value` is in no list through this hook.
  void push_back(T& value) noexcept { (void)insert(end(), value); }

  /// @brief Links `value` directly before `pos`.
  /// @pre `pos` is an element of this list or its `end()`, and `value` is in
  ///      no list through this hook.
  /// @return An iterator to `value`.
  iterator insert(const_iterator pos, T& value) noexcept {
    link& node = link_of(value);
    METL_HARDEN(node.owner == nullptr);  // already linked
    METL_HARDEN(in_this_list(pos));      // a position in another list, or none
    metl::detail::dlist_link_before(*pos.node_, node);
    node.owner = this;
    ++size_;
    return iterator(&node);
  }

  /// @brief Unlinks the first element.
  /// @pre `!empty()`.
  void pop_front() noexcept { (void)erase(begin()); }

  /// @brief Unlinks the last element.
  /// @pre `!empty()`.
  void pop_back() noexcept { (void)erase(const_iterator(head_.prev)); }

  /// @brief Unlinks the element at `pos`.
  /// @pre `pos` is an element of this list, not its `end()`.
  /// @return An iterator to the element after the unlinked one, or `end()`.
  iterator erase(const_iterator pos) noexcept {
    METL_HARDEN(in_this_list(pos));    // a position in another list, or none
    METL_HARDEN(pos.node_ != &head_);  // end(): there is no element to unlink
    link* const next = pos.node_->next;
    unlink(*pos.node_);
    return iterator(next);
  }

  /// @brief Unlinks `value`, wherever it is in this list.
  /// @pre `value` is in this list.
  void erase(T& value) noexcept {
    link& node = link_of(value);
    METL_HARDEN(node.owner == this);  // in another list, or in none
    unlink(node);
  }

  /// @brief Moves the element at `it`, in `other`, to directly before `pos` in
  ///        this list. `other` may be this list. The element is never unlinked
  ///        and iterators to it stay valid; moved to another list, it belongs
  ///        to that list.
  /// @pre `pos` is an element of this list or its `end()`, and `it` is an
  ///      element of `other`, not its `end()`.
  void splice(const_iterator pos, intrusive_list& other, const_iterator it) noexcept {
    METL_HARDEN(in_this_list(pos));         // a position in another list, or none
    METL_HARDEN(other.in_this_list(it));    // an element of another list, or none
    METL_HARDEN(it.node_ != &other.head_);  // end(): there is no element to move
    if (pos.node_ == it.node_ || pos.node_ == it.node_->next) {
      return;  // already directly before `pos`
    }
    metl::detail::dlist_unlink(*it.node_);
    metl::detail::dlist_link_before(*pos.node_, *it.node_);
    if (&other != this) {
      it.node_->owner = this;
      --other.size_;
      ++size_;
    }
  }

  /// @brief Unlinks every element.
  void clear() noexcept {
    while (head_.next != &head_) {
      unlink(*head_.next);
    }
  }

 private:
  static link& link_of(T& value) noexcept { return static_cast<link&>(static_cast<hook_type&>(value)); }
  static T& value_of(link& node) noexcept { return static_cast<T&>(static_cast<hook_type&>(node)); }

  METL_NODISCARD bool in_this_list(const_iterator pos) const noexcept {
    return pos.node_ != nullptr && pos.node_->owner == this;
  }

  void unlink(link& node) noexcept {
    metl::detail::dlist_unlink(node);
    node.owner = nullptr;
    --size_;
  }

  link head_;
  size_type size_ = 0;
};

}  // namespace metl
