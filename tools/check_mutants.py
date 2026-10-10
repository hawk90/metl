#!/usr/bin/env python3
"""Break the library on purpose and require the gates to notice.

WHY. Coverage says a line RAN. It does not say a test would notice if the line
were wrong, and this repository had a live example: `flat_set::nth(i)` returning
element `i+1` survived every ctest target. The line was covered. Nothing checked
what it returned, until the fuzz harness's oracle was made runnable under ctest
(replay_fuzz_flat_set) -- see the map below.

The repository already argues this everywhere else. Every checker under tools/
carries a `--self-test` because "a gate that cannot fail is not a gate"; the
compile-failure cases exist because a `static_assert` gone always-true is
invisible. Both are mutation testing, applied to the checkers and to the
contracts. This applies it to the library.

WHAT IT PRODUCES, and this is the part worth reading: a map of which gate
catches which class of defect. It is not what anyone would guess.

    flat_map::find -> neighbour        killed by unit tests, NOT by the fuzzer
                                       before #82 added a reference model
    flat_set::nth(i) -> i+1            killed ONLY by the fuzz harness's oracle.
                                       It survived every other ctest target, and
                                       was unreachable from here at all until
                                       fuzz/replay_main.cpp let the harnesses run
                                       without libFuzzer.
    binary search -> linear scan       killed ONLY by the operation-count test;
                                       results are identical, so correctness
                                       tests and the fuzz oracle both pass
    power-of-two assert -> always true  killed ONLY by the compile-failure cases

No layer dominates another. That is a better argument for keeping all of them
than any of the individual PRs made.

RULES.

  * Every mutant must be KILLED. A survivor is a missing test, not a tolerable
    outcome, so there is no allowlist to grow.
  * Every mutant names the gate expected to kill it.
  * The tree is restored whatever happens, and the restoration is verified by
    content rather than assumed. A mutation tool that leaves a mutant behind is
    worse than no tool.

Usage:
    tools/check_mutants.py --build-dir build
    tools/check_mutants.py --build-dir build --only linear_scan
    tools/check_mutants.py --self-test
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

REPO = pathlib.Path(__file__).resolve().parent.parent

# Per-test ceiling while a mutant is applied; see run_gate.
CTEST_TIMEOUT_SECONDS = 120


# Each mutant is a single, minimal edit that changes BEHAVIOUR and keeps the code
# compiling. `kills` names the gate that must reject it.
#
#   ctest:<regex>   ctest -R <regex> must fail
#   tool:<argv>     the command must exit non-zero
MUTANTS = [
    {
        "name": "flat_map_linear_scan",
        "file": "include/metl/flat_map.hpp",
        "why": "binary search becomes a linear scan. Same results, same order, "
               "same sizes -- only the comparison count moves.",
        "kills": "ctest:operation_count",
        "old": """    size_type first = 0;
    size_type count = size_;
    while (count > 0) {
      const size_type step = count / 2;
      const size_type index = first + step;
      if (comp_(data()[index].key, key)) {
        first = index + 1;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return first;
  }

  template <typename K>
  size_type upper_bound_index""",
        "new": """    size_type first = 0;
    while (first < size_ && comp_(data()[first].key, key)) {
      ++first;
    }
    return first;
  }

  template <typename K>
  size_type upper_bound_index""",
    },
    {
        "name": "flat_map_find_neighbour",
        "file": "include/metl/flat_map.hpp",
        "why": "find returns the NEXT element's value. Keys stay sorted, size "
               "stays right, find and contains still agree with each other.",
        "kills": "ctest:flat_map|recoverable_api",
        "old": """  METL_NODISCARD mapped_type* find(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return detail::addressof(data()[index].value);
    }""",
        "new": """  METL_NODISCARD mapped_type* find(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return detail::addressof(data()[index + 1 < size_ ? index + 1 : index].value);
    }""",
    },
    {
        "name": "fixed_string_append_drops_last",
        "file": "include/metl/fixed_string.hpp",
        "why": "try_append copies one character too few but reports the full "
               "length. The NUL terminator is still where size() says.",
        "kills": "ctest:fixed_string|format",
        "old": """    copy_in(size_, text, input_size);
""",
        "new": """    copy_in(size_, text, input_size == 0 ? 0 : input_size - 1);
""",
    },
    {
        "name": "flat_set_nth_off_by_one",
        "file": "include/metl/flat_set.hpp",
        "why": "positional access returns the NEXT element. This one survives "
               "all ctest targets except the replay -- the harness oracle is "
               "the only thing that looks at what nth() returns.",
        "kills": "ctest:replay_fuzz_flat_set",
        "old": """  METL_NODISCARD const_reference nth(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }""",
        "new": """  METL_NODISCARD const_reference nth(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index + 1 < size_ ? index + 1 : index];
  }""",
    },
    {
        "name": "spsc_capacity_assert_always_true",
        "file": "include/metl/spsc_queue.hpp",
        "why": "the power-of-two contract becomes unenforceable. Nothing in "
               "ctest notices, because no test constructs an invalid capacity "
               "-- it could not, it would not compile.",
        "kills": "tool:tools/check_compile_fail.py --cxx clang++",
        "old": '  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");',
        "new": '  static_assert(true || ((Capacity & (Capacity - 1)) == 0), "Capacity must be power of two");',
    },
    {
        "name": "visit_single_result_guard_removed",
        "file": "include/metl/variant.hpp",
        "why": "#84's guard becomes vacuous, so metl::visit goes back to "
               "silently truncating a visitor's result.",
        "kills": "tool:tools/check_compile_fail.py --cxx clang++",
        "old": """template <typename Visitor, typename... Ts>
inline constexpr bool visit_single_result_lvalue_v =
    (std::is_same_v<visit_result_lvalue_t<Visitor, Ts...>,
                    decltype(std::declval<Visitor>()(std::declval<Ts&>()))> &&
     ...);""",
        "new": """template <typename Visitor, typename... Ts>
inline constexpr bool visit_single_result_lvalue_v = true;""",
    },
    {
        "name": "fixed_vector_range_erase_skips_dtor",
        "file": "include/metl/fixed_vector.hpp",
        "why": "range erase shifts the survivors down but never destroys the "
               "vacated tail, so those objects leak.",
        "kills": "ctest:fixed_vector_test|sequence_reentrancy",
        "old": """    for (size_type i = size_; i < old_size; ++i) {
      data()[i].~T();
    }""",
        "new": """    for (size_type i = size_; i < old_size; ++i) {
    }""",
    },
    {
        "name": "fixed_vector_try_assign_n_rejects_exact",
        "file": "include/metl/fixed_vector.hpp",
        "why": "try_assign(n, value) refuses n == Capacity, which fits.",
        "kills": "ctest:fixed_vector_test",
        "old": """  METL_NODISCARD bool try_assign(size_type n, const T& value) {
    if (n > Capacity) {""",
        "new": """  METL_NODISCARD bool try_assign(size_type n, const T& value) {
    if (n >= Capacity) {""",
    },
    {
        "name": "fixed_vector_try_assign_range_rejects_exact",
        "file": "include/metl/fixed_vector.hpp",
        "why": "try_assign(first, last) refuses a range of exactly Capacity.",
        "kills": "ctest:fixed_vector_test",
        "old": """    if (static_cast<size_type>(std::distance(first, last)) > Capacity) {
      return false;
    }
    assign(first, last);""",
        "new": """    if (static_cast<size_type>(std::distance(first, last)) >= Capacity) {
      return false;
    }
    assign(first, last);""",
    },
    {
        "name": "fixed_vector_equal_ignores_size",
        "file": "include/metl/fixed_vector.hpp",
        "why": "operator== treats a vector as equal to any longer one it is a "
               "prefix of.",
        "kills": "ctest:fixed_vector_test",
        "old": """  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!(lhs[i] == rhs[i])) {""",
        "new": """  if (lhs.size() > rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!(lhs[i] == rhs[i])) {""",
    },
    {
        "name": "handle_pool_const_get_past_end",
        "file": "include/metl/handle_pool.hpp",
        "why": "the const get() turns the 'does not resolve' index into "
               "slot_ptr(Capacity), so a stale or null handle returns a "
               "pointer one past the slots.",
        "kills": "ctest:handle_pool_test",
        "old": """  METL_NODISCARD const_pointer get(handle_type handle) const noexcept {
    const size_type index = live_index(handle);
    return index < Capacity ? slot_ptr(index) : nullptr;""",
        "new": """  METL_NODISCARD const_pointer get(handle_type handle) const noexcept {
    const size_type index = live_index(handle);
    return index <= Capacity ? slot_ptr(index) : nullptr;""",
    },
    {
        "name": "handle_pool_range_and_inactive",
        "file": "include/metl/handle_pool.hpp",
        "why": "the liveness check needs out-of-range AND inactive to reject, "
               "so a forged handle to a never-used slot at its starting "
               "generation resolves.",
        "kills": "ctest:handle_pool_test",
        "old": "if (index >= Capacity || !active_[index] || generation_[index] != handle.generation()) {",
        "new": "if ((index >= Capacity && !active_[index]) || generation_[index] != handle.generation()) {",
    },
    {
        "name": "arena_exact_fit_record_rejected",
        "file": "include/metl/arena_allocator.hpp",
        "why": "an allocation whose payload and record fill the arena to the "
               "last byte is refused.",
        "kills": "ctest:arena_allocator_test",
        "old": "    if (sizeof(allocation_record) > left) {",
        "new": "    if (sizeof(allocation_record) >= left) {",
    },
    {
        "name": "arena_offset_skips_a_byte",
        "file": "include/metl/arena_allocator.hpp",
        "why": "every allocation consumes one byte more than it reports; "
               "rewind and reset stay self-consistent, only used() and the "
               "offsets move.",
        "kills": "ctest:arena_allocator_test",
        "old": "    const size_type next_offset = previous_offset + total_bytes;",
        "new": "    const size_type next_offset = previous_offset + total_bytes + 1;",
    },
    {
        "name": "spsc_byte_ring_clear_keeps_head",
        "file": "include/metl/spsc_byte_ring.hpp",
        "why": "clear() resets only the write index; after any consume the "
               "read index is left ahead of it and readable_size() wraps.",
        "kills": "ctest:spsc_byte_ring_test",
        "old": """  void clear() noexcept {
    head_.store(0, std::memory_order_relaxed);
""",
        "new": """  void clear() noexcept {
""",
    },
    {
        "name": "unordered_map_copy_assign_keeps_hasher",
        "file": "include/metl/static_unordered_map.hpp",
        "why": "copy assignment keeps the destination's hasher instead of the "
               "source's, unlike the copy constructor.",
        "kills": "ctest:static_unordered_map_test",
        "old": """    clear();
    hasher_ = other.hasher_;
""",
        "new": """    clear();
""",
    },
    {
        "name": "unordered_map_heterogeneous_erase_missing_true",
        "file": "include/metl/static_unordered_map.hpp",
        "why": "heterogeneous erase of an absent key reports that it erased "
               "something.",
        "kills": "ctest:static_unordered_map_test",
        "old": """  bool erase(const K& key) noexcept(lookup_cannot_throw<K>) {
    const size_type index = find_existing_index(key);
    if (index == npos) {
      return false;""",
        "new": """  bool erase(const K& key) noexcept(lookup_cannot_throw<K>) {
    const size_type index = find_existing_index(key);
    if (index == npos) {
      return true;""",
    },
    {
        "name": "flat_map_less_ignores_greater_key",
        "file": "include/metl/flat_map.hpp",
        "why": "operator< no longer stops at a greater lhs key, so a later "
               "value or the size decides instead.",
        "kills": "ctest:flat_map_test",
        "old": """    if (rhs.begin()[i].key < lhs.begin()[i].key) {
      return false;
    }""",
        "new": """    if (rhs.begin()[i].key < lhs.begin()[i].key && false) {
      return false;
    }""",
    },
    {
        "name": "flat_map_less_ignores_greater_value",
        "file": "include/metl/flat_map.hpp",
        "why": "operator< no longer stops at a greater lhs value, so the size "
               "decides instead.",
        "kills": "ctest:flat_map_test",
        "old": """    if (rhs.begin()[i].value < lhs.begin()[i].value) {
      return false;
    }""",
        "new": """    if (rhs.begin()[i].value < lhs.begin()[i].value && false) {
      return false;
    }""",
    },
    {
        "name": "stepper_poll_reruns_finished_task",
        "file": "include/metl/coro/stepper.hpp",
        "why": "poll() keeps calling step() on a task that finished with done. "
               "It still returns false; only the extra step() calls show.",
        "kills": "ctest:coro_stepper_test",
        "old": "    if (done_ || error_) {",
        "new": "    if (error_) {",
    },
    {
        "name": "lookup_table_index_returns_first",
        "file": "include/metl/lookup_table.hpp",
        "why": "operator[] returns entry 0 whatever the index. The constexpr "
               "checks never call it, and the one other call reads a "
               "character entry 0 happens to share.",
        "kills": "ctest:lookup_table_test",
        "old": """operator[](size_type index) const noexcept {
    METL_ASSERT(index < Size);
    return entries_[index];""",
        "new": """operator[](size_type index) const noexcept {
    METL_ASSERT(index < Size);
    return entries_[0];""",
    },
    {
        "name": "spsc_byte_ring_consume_harden_removed",
        "file": "include/metl/spsc_byte_ring.hpp",
        "why": "consume() past the readable bytes moves the read index beyond "
               "the write index at METL_HARDENING_NONE.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(count <= tail - head);",
        "new": "",
    },
    {
        "name": "arena_alignment_harden_removed",
        "file": "include/metl/arena_allocator.hpp",
        "why": "a non-power-of-two alignment reaches align_up's bitmask at "
               "METL_HARDENING_NONE.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(alignment != 0 && (alignment & (alignment - 1)) == 0);",
        "new": "",
    },
    {
        "name": "flat_map_emplace_full_harden_removed",
        "file": "include/metl/flat_map.hpp",
        "why": "emplace of a new key into a full flat_map returns a one-past- "
               "the-end reference at METL_HARDENING_NONE.",
        "kills": "ctest:harden_floor_memory",
        "old": """      // stripped at low hardening levels; METL_HARDEN never is.
      METL_HARDEN(index < size_);""",
        "new": "      // stripped at low hardening levels; METL_HARDEN never is.",
    },
    {
        "name": "flat_map_insert_or_assign_full_harden_removed",
        "file": "include/metl/flat_map.hpp",
        "why": "insert_or_assign of a new key into a full flat_map returns a "
               "one-past-the-end reference at METL_HARDENING_NONE.",
        "kills": "ctest:harden_floor_memory",
        "old": """      // `index == size_`, which would make this a one-past-the-end reference.
      METL_HARDEN(index < size_);""",
        "new": "      // `index == size_`, which would make this a one-past-the-end reference.",
    },
    {
        "name": "flat_set_emplace_full_harden_removed",
        "file": "include/metl/flat_set.hpp",
        "why": "emplace of a new key into a full flat_set returns a one-past- "
               "the-end reference at METL_HARDENING_NONE.",
        "kills": "ctest:harden_floor_memory",
        "old": """      // levels; METL_HARDEN never is.
      METL_HARDEN(index < size_);""",
        "new": "      // levels; METL_HARDEN never is.",
    },
    {
        "name": "fixed_string_compare_null_harden_removed",
        "file": "include/metl/fixed_string.hpp",
        "why": "comparing a fixed_string with a null const char* reads "
               "through the null pointer at METL_HARDENING_NONE.",
        "kills": "ctest:harden_floor_memory",
        "old": """  int compare_c(const char* text) const noexcept {
    METL_HARDEN(text != nullptr);""",
        "new": """  int compare_c(const char* text) const noexcept {""",
    },
    {
        "name": "unordered_map_insert_or_assign_full_harden_removed",
        "file": "include/metl/static_unordered_map.hpp",
        "why": "insert_or_assign of a new key into a full map returns "
               "*slot_value(npos) at METL_HARDENING_NONE.",
        "kills": "ctest:harden_floor_memory",
        "old": """      // hardening levels; without this, slot_value(npos) would be a wild read.
      METL_HARDEN(index < bucket_count);""",
        "new": """      // hardening levels; without this, slot_value(npos) would be a wild read.""",
    },
    # ---- the rest of the METL_HARDEN floor (#241) ------------------------------
    {
        "name": "atomic_ref_alignment_harden_removed",
        "file": "include/metl/atomic_ref.hpp",
        "why": "an object aligned for T but not for std::atomic<T> is accessed "
               "by a misaligned atomic instruction.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN((reinterpret_cast<std::uintptr_t>(ptr_) % required_alignment) == 0u);",
        "new": "",
    },
    {
        "name": "fixed_function_noexcept_call_empty_harden_removed",
        "file": "include/metl/fixed_function.hpp",
        "why": "calling an empty noexcept fixed_function jumps through a null "
               "ops table.",
        "kills": "ctest:harden_floor_memory",
        "old": """  fixed_function& operator=(F&& function) {
    assign(std::forward<F>(function));
    return *this;
  }

  /// @brief Invokes the stored callable.
  /// @pre A target must be stored (has_value() is true); else asserts.
  R operator()(Args... args) const noexcept {
    METL_HARDEN(this->ops_ != nullptr);""",
        "new": """  fixed_function& operator=(F&& function) {
    assign(std::forward<F>(function));
    return *this;
  }

  /// @brief Invokes the stored callable.
  /// @pre A target must be stored (has_value() is true); else asserts.
  R operator()(Args... args) const noexcept {""",
    },
    {
        "name": "fixed_any_invocable_noexcept_call_empty_harden_removed",
        "file": "include/metl/fixed_function.hpp",
        "why": "calling an empty noexcept fixed_any_invocable jumps through a "
               "null ops table.",
        "kills": "ctest:harden_floor_memory",
        "old": """  fixed_any_invocable& operator=(F&& function) {
    assign(std::forward<F>(function));
    return *this;
  }

  /// @brief Invokes the stored callable.
  /// @pre A target must be stored (has_value() is true); else asserts.
  R operator()(Args... args) const noexcept {
    METL_HARDEN(this->ops_ != nullptr);""",
        "new": """  fixed_any_invocable& operator=(F&& function) {
    assign(std::forward<F>(function));
    return *this;
  }

  /// @brief Invokes the stored callable.
  /// @pre A target must be stored (has_value() is true); else asserts.
  R operator()(Args... args) const noexcept {""",
    },
    {
        "name": "function_ref_call_empty_harden_removed",
        "file": "include/metl/function_ref.hpp",
        "why": "calling an empty function_ref jumps to address 0.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(callback_ != nullptr);",
        "new": "",
    },
    {
        "name": "parse_int_harden_removed",
        "file": "include/metl/parse.hpp",
        "why": "the asserting parse_int returns a result built from the error.",
        "kills": "ctest:harden_floor_memory",
        "old": """  const expected<parsed<T>, parse_error> result = try_parse_int<T>(text);
  METL_HARDEN(result.has_value());""",
        "new": "  const expected<parsed<T>, parse_error> result = try_parse_int<T>(text);",
    },
    {
        "name": "parse_hex_harden_removed",
        "file": "include/metl/parse.hpp",
        "why": "the asserting parse_hex returns a result built from the error.",
        "kills": "ctest:harden_floor_memory",
        "old": """  const expected<parsed<T>, parse_error> result = try_parse_hex<T>(text);
  METL_HARDEN(result.has_value());""",
        "new": "  const expected<parsed<T>, parse_error> result = try_parse_hex<T>(text);",
    },
    {
        "name": "span_pointer_pair_reversed_harden_removed",
        "file": "include/metl/span.hpp",
        "why": "a reversed pointer pair makes a span whose size() wraps to "
               "nearly SIZE_MAX.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(last >= first);",
        "new": "",
    },
    {
        "name": "span_pointer_pair_extent_harden_removed",
        "file": "include/metl/span.hpp",
        "why": "a fixed extent over a shorter pointer pair reports more "
               "elements than exist.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(Extent == dynamic_extent || static_cast<size_type>(last - first) == Extent);",
        "new": "",
    },
    {
        "name": "span_container_extent_harden_removed",
        "file": "include/metl/span.hpp",
        "why": "a fixed extent over a smaller container reports more elements "
               "than exist.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(Extent == dynamic_extent || container.size() == Extent);",
        "new": "",
    },
    {
        "name": "span_converting_extent_harden_removed",
        "file": "include/metl/span.hpp",
        "why": "a fixed extent over a shorter span reports more elements than "
               "exist.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(Extent == dynamic_extent || other.size() == Extent);",
        "new": "",
    },
    {
        "name": "span_subspan_offset_harden_removed",
        "file": "include/metl/span.hpp",
        "why": "subspan<Offset>() past the end views memory beyond the "
               "storage.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(Offset <= size());",
        "new": "",
    },
    {
        "name": "span_subspan_count_harden_removed",
        "file": "include/metl/span.hpp",
        "why": "subspan<Offset, Count>() longer than what remains views memory "
               "beyond the storage.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(actual_count <= (size() - Offset));",
        "new": "",
    },
    {
        "name": "span_last_count_harden_removed",
        "file": "include/metl/span.hpp",
        "why": "last<Count>() longer than the span starts before the storage.",
        "kills": "ctest:harden_floor_memory",
        "old": """"last<Count>(): Count must not exceed Extent");
    METL_HARDEN(Count <= size());""",
        "new": "\"last<Count>(): Count must not exceed Extent\");",
    },
    {
        "name": "fixed_vector_try_emplace_pos_harden_removed",
        "file": "include/metl/fixed_vector.hpp",
        "why": "try_emplace at a position past end() on a full vector returns "
               "end() instead of reporting the bad position.",
        "kills": "ctest:harden_floor_memory",
        "old": """  METL_NODISCARD iterator try_emplace(const_iterator pos, Args&&... args) {
    METL_HARDEN(pos >= begin() && pos <= end());""",
        "new": "  METL_NODISCARD iterator try_emplace(const_iterator pos, Args&&... args) {",
    },
    {
        "name": "fixed_vector_try_insert_count_pos_harden_removed",
        "file": "include/metl/fixed_vector.hpp",
        "why": "the same for try_insert(pos, n, value).",
        "kills": "ctest:harden_floor_memory",
        "old": """  METL_NODISCARD iterator try_insert(const_iterator pos, size_type n, const T& value) {
    METL_HARDEN(pos >= begin() && pos <= end());""",
        "new": "  METL_NODISCARD iterator try_insert(const_iterator pos, size_type n, const T& value) {",
    },
    {
        "name": "fixed_vector_try_insert_range_pos_harden_removed",
        "file": "include/metl/fixed_vector.hpp",
        "why": "the same for try_insert(pos, first, last).",
        "kills": "ctest:harden_floor_memory",
        "old": """        "before anything is written, so that a failure leaves contents unchanged");
    METL_HARDEN(pos >= begin() && pos <= end());""",
        "new": "        \"before anything is written, so that a failure leaves contents unchanged\");",
    },
    {
        "name": "fixed_vector_insert_count_pos_harden_removed",
        "file": "include/metl/fixed_vector.hpp",
        "why": "insert(pos, 0, value) past end() returns an iterator outside "
               "the vector.",
        "kills": "ctest:harden_floor_memory",
        "old": """  iterator insert(const_iterator pos, size_type n, const T& value) {
    METL_HARDEN(pos >= begin() && pos <= end());""",
        "new": "  iterator insert(const_iterator pos, size_type n, const T& value) {",
    },
    {
        "name": "fixed_vector_insert_range_pos_harden_removed",
        "file": "include/metl/fixed_vector.hpp",
        "why": "insert(pos, first, first) past end() returns an iterator "
               "outside the vector.",
        "kills": "ctest:harden_floor_memory",
        "old": """  iterator insert(const_iterator pos, It first, It last) {
    METL_HARDEN(pos >= begin() && pos <= end());""",
        "new": "  iterator insert(const_iterator pos, It first, It last) {",
    },
    {
        "name": "fixed_priority_queue_pop_empty_harden_removed",
        "file": "include/metl/fixed_priority_queue.hpp",
        "why": "pop on an empty queue moves from index SIZE_MAX into slot 0 "
               "before the vector's own check fires.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(!storage_.empty());",
        "new": "",
    },
    {
        "name": "unordered_map_place_full_harden_removed",
        "file": "include/metl/static_unordered_map.hpp",
        "why": "a new key into a table with every bucket taken is constructed "
               "at slot npos.",
        "kills": "ctest:harden_floor_memory",
        "old": """    // wild out-of-bounds construct_at(npos, ...).
    METL_HARDEN(index < bucket_count);""",
        "new": "    // wild out-of-bounds construct_at(npos, ...).",
    },
    {
        "name": "unordered_set_place_full_harden_removed",
        "file": "include/metl/static_unordered_set.hpp",
        "why": "a new key into a table with every bucket taken is constructed "
               "at slot npos.",
        "kills": "ctest:harden_floor_memory",
        "old": """    // a user-disabled METL_ASSERT.
    METL_HARDEN(index < bucket_count);""",
        "new": "    // a user-disabled METL_ASSERT.",
    },
    # ---- metl::expected (#202) ------------------------------------------------
    {
        "name": "expected_move_assign_self_check_inverted",
        "file": "include/metl/expected.hpp",
        "why": "move assignment returns early for every OTHER object, so it is "
               "a no-op. Live-object counts stay balanced; only the value is "
               "wrong, and only the fuzz harness's value oracle reads it.",
        "kills": "ctest:replay_fuzz_vocab",
        "old": """                                                 std::is_nothrow_move_constructible_v<E>) {
    if (this == &other) {""",
        "new": """                                                 std::is_nothrow_move_constructible_v<E>) {
    if (this != &other) {""",
    },
    {
        "name": "expected_move_ctor_branches_swapped",
        "file": "include/metl/expected.hpp",
        "why": "the move constructor builds the error from a value and the "
               "value from an error. Copy elision means a prvalue never reaches "
               "it, so only a test that moves a NAMED expected instantiates it.",
        "kills": "ctest:expected_test",
        "old": """    if (has_value_) {
      construct_value(static_cast<T&&>(other.value_unchecked()));
    } else {
      construct_error(static_cast<E&&>(other.error_unchecked()));
    }""",
        "new": """    if (!has_value_) {
      construct_value(static_cast<T&&>(other.value_unchecked()));
    } else {
      construct_error(static_cast<E&&>(other.error_unchecked()));
    }""",
    },
    {
        "name": "expected_void_copy_assign_self_check_inverted",
        "file": "include/metl/expected.hpp",
        "why": "expected<void, E> copy assignment becomes a no-op for every "
               "other object; the state and the error are never copied.",
        "kills": "ctest:expected_test",
        "old": """  expected& operator=(const expected& other) {
    if (this == &other) {
      return *this;
    }

    if (other.has_value_) {
      if (!has_value_) {""",
        "new": """  expected& operator=(const expected& other) {
    if (this != &other) {
      return *this;
    }

    if (other.has_value_) {
      if (!has_value_) {""",
    },
    {
        "name": "expected_swap_value_error_leaks_error",
        "file": "include/metl/expected.hpp",
        "why": "the value<->error swap (E nothrow-movable) never destroys the "
               "old error. Both sides read right afterwards; one E is leaked, "
               "which only a lifetime count sees.",
        "kills": "ctest:expected_test",
        "old": """      E tmp(static_cast<E&&>(*e.error_ptr()));
      e.error_ptr()->~E();""",
        "new": """      E tmp(static_cast<E&&>(*e.error_ptr()));""",
    },
    {
        "name": "expected_swap_value_error_leaks_error_throwing_move",
        "file": "include/metl/expected.hpp",
        "why": "the same leak on the other branch, taken when only T is "
               "nothrow-movable: the old error is overwritten, not destroyed.",
        "kills": "ctest:expected_test",
        "old": """      e.error_ptr()->~E();
      ::new (e.storage_.value_storage.addr()) T(static_cast<T&&>(tmp));""",
        "new": """      ::new (e.storage_.value_storage.addr()) T(static_cast<T&&>(tmp));""",
    },
    {
        "name": "expected_error_copy_assign_dropped",
        "file": "include/metl/expected.hpp",
        "why": "assigning an error to an expected that already holds one keeps "
               "the OLD error. The state is right, so has_value() checks pass.",
        "kills": "ctest:expected_test",
        "old": """      *error_ptr() = std::forward<G>(error);
      return;""",
        "new": """      return;""",
    },
    {
        "name": "expected_error_error_swap_dropped",
        "file": "include/metl/expected.hpp",
        "why": "swapping two expecteds that both hold errors does nothing.",
        "kills": "ctest:expected_test",
        "old": """      swap(*value_ptr(), *other.value_ptr());
    } else if (!has_value_ && !other.has_value_) {
      using std::swap;
      swap(*error_ptr(), *other.error_ptr());
    } else if (has_value_ && !other.has_value_) {""",
        "new": """      swap(*value_ptr(), *other.value_ptr());
    } else if (!has_value_ && !other.has_value_) {
    } else if (has_value_ && !other.has_value_) {""",
    },
    {
        "name": "expected_emplace_move_throw_no_restore",
        "file": "include/metl/expected.hpp",
        "why": "when the move that commits a new value throws, the backed-up "
               "error is not put back: a destroyed E under has_value() == false.",
        "kills": "ctest:expected_regression",
        "old": """        construct_error(static_cast<E&&>(backup));
        throw;""",
        "new": """        throw;""",
    },
    {
        "name": "expected_emplace_pinned_throw_no_restore",
        "file": "include/metl/expected.hpp",
        "why": "the same for a non-movable T whose constructor throws: the "
               "previous error is destroyed and never restored.",
        "kills": "ctest:expected_regression",
        "old": """        construct_error(static_cast<E&&>(backup));  // restore the previous state""",
        "new": """        // restore the previous state""",
    },
    # ---- metl::variant (#202) -------------------------------------------------
    {
        "name": "variant_visit_rvalue_same_index",
        "file": "include/metl/variant.hpp",
        "why": "rvalue visit recurses on the SAME index, so any alternative "
               "but the first never terminates. Every test visited index 0.",
        "kills": "ctest:variant_test",
        "old": """    return visit_impl<Result, Index + 1>(std::forward<Visitor>(visitor),
                                         static_cast<variant<Ts...>&&>(value));""",
        "new": """    return visit_impl<Result, Index>(std::forward<Visitor>(visitor),
                                     static_cast<variant<Ts...>&&>(value));""",
    },
    {
        "name": "variant_visit_const_rvalue_same_index",
        "file": "include/metl/variant.hpp",
        "why": "the same on the const-rvalue overload.",
        "kills": "ctest:variant_test",
        "old": """    return visit_impl<Result, Index + 1>(std::forward<Visitor>(visitor),
                                         static_cast<const variant<Ts...>&&>(value));""",
        "new": """    return visit_impl<Result, Index>(std::forward<Visitor>(visitor),
                                     static_cast<const variant<Ts...>&&>(value));""",
    },
    {
        "name": "variant_compare_same_index",
        "file": "include/metl/variant.hpp",
        "why": "comparison recurses on the SAME index, so comparing two "
               "variants that both hold alternative 1 or later never returns.",
        "kills": "ctest:variant_test",
        "old": "    return compare_alternative<Op, I + 1>(lhs, rhs);",
        "new": "    return compare_alternative<Op, I>(lhs, rhs);",
    },
    {
        "name": "variant_valueless_equal_flipped",
        "file": "include/metl/variant.hpp",
        "why": "two valueless variants compare unequal.",
        "kills": "ctest:variant_regression",
        "old": """  if (lhs.valueless_by_exception()) {
    return true;
  }
  return detail::compare_alternative<detail::cmp_eq, 0>(lhs, rhs);""",
        "new": """  if (lhs.valueless_by_exception()) {
    return false;
  }
  return detail::compare_alternative<detail::cmp_eq, 0>(lhs, rhs);""",
    },
    {
        "name": "variant_valueless_not_equal_flipped",
        "file": "include/metl/variant.hpp",
        "why": "two valueless variants compare not-equal.",
        "kills": "ctest:variant_regression",
        "old": """  if (lhs.valueless_by_exception()) {
    return false;
  }
  return detail::compare_alternative<detail::cmp_ne, 0>(lhs, rhs);""",
        "new": """  if (lhs.valueless_by_exception()) {
    return true;
  }
  return detail::compare_alternative<detail::cmp_ne, 0>(lhs, rhs);""",
    },
    {
        "name": "variant_copy_assign_from_valueless_keeps_old",
        "file": "include/metl/variant.hpp",
        "why": "copy-assigning a valueless variant leaves the target holding "
               "its old alternative instead of becoming valueless.",
        "kills": "ctest:variant_regression",
        "old": """      reset();
      return *this;
    }
    assign_from(other);""",
        "new": """      return *this;
    }
    assign_from(other);""",
    },
    {
        "name": "variant_move_assign_from_valueless_keeps_old",
        "file": "include/metl/variant.hpp",
        "why": "the same for move assignment.",
        "kills": "ctest:variant_regression",
        "old": """      reset();
      return *this;
    }
    assign_from(static_cast<variant&&>(other));""",
        "new": """      return *this;
    }
    assign_from(static_cast<variant&&>(other));""",
    },
    {
        "name": "scheduler_duplicate_attach_harden_removed",
        "file": "include/metl/coro/scheduler.hpp",
        "why": "a task attached twice keeps an entry after detach() removes "
               "one, so the scheduler polls an object the caller destroyed.",
        "kills": "ctest:harden_floor_memory",
        "old": "    METL_HARDEN(!is_attached(task));",
        "new": "",
    },
    {
        "name": "event_dispatcher_calls_mid_dispatch_subscriber",
        "file": "include/metl/event_dispatcher.hpp",
        "why": "a listener subscribed during dispatch hears the current event "
               "or not depending on the slot it lands in.",
        "kills": "ctest:event_dispatcher_reentrancy",
        "old": "    const std::uint64_t first_unheard = next_id_;",
        "new": "    const std::uint64_t first_unheard = ~std::uint64_t{0};",
    },
    {
        "name": "flat_map_clear_relocates",
        "file": "include/metl/flat_map.hpp",
        "why": "clear() moves each element into a temporary before destroying it: "
               "N needless moves, and std::terminate when the move throws.",
        "kills": "ctest:throwing_element",
        "old": """    while (size_ > 0) {
      --size_;
      data()[size_].~value_type();
    }""",
        "new": """    while (size_ > 0) {
      erase_at(size_ - 1);
    }""",
    },
    {
        "name": "flat_set_clear_relocates",
        "file": "include/metl/flat_set.hpp",
        "why": "clear() moves each element into a temporary before destroying it: "
               "N needless moves, and std::terminate when the move throws.",
        "kills": "ctest:throwing_element",
        "old": """    while (size_ > 0) {
      --size_;
      data()[size_].~value_type();
    }""",
        "new": """    while (size_ > 0) {
      erase_at(size_ - 1);
    }""",
    },
]


def force_rebuild(path):
    """Make `path` unambiguously newer than anything already built from it.

    Found the hard way. This tool mutates, builds, reverts and mutates again in
    well under a second, and make compares mtimes at one-second granularity --
    so the SECOND mutant's build was skipped and its verdict came from the
    PREVIOUS binaries. Every mutant after the first reported SURVIVED.
    Reproduced exactly: flat_map_find_neighbour is killed when run with --only
    and "survives" when run second.

    A mutation tool that silently tests the wrong binary is the failure mode
    this whole repository exists to remove, so the fix is not "sleep" -- it is
    to put the timestamp beyond argument.
    """
    stamp = time.time() + 10
    os.utime(path, (stamp, stamp))


def apply_mutant(mutant):
    """Patch the file. Returns the original text so it can be restored."""
    path = REPO / mutant["file"]
    original = path.read_text(encoding="utf-8")
    occurrences = original.count(mutant["old"])
    if occurrences != 1:
        raise RuntimeError(
            f"{mutant['name']}: its anchor matched {occurrences} times in "
            f"{mutant['file']}, expected exactly 1. The code moved; re-read the "
            f"mutant rather than loosening the anchor.")
    path.write_text(original.replace(mutant["old"], mutant["new"]), encoding="utf-8")
    force_rebuild(path)
    return original


def run_gate(spec, build_dir):
    """True if the gate REJECTED the tree (i.e. the mutant was killed)."""
    kind, _, rest = spec.partition(":")
    if kind == "ctest":
        build = subprocess.run(["cmake", "--build", build_dir, "-j"],
                               capture_output=True, text=True, cwd=REPO)
        if build.returncode != 0:
            # A mutant that stops the build is not a useful mutant: it proves the
            # compiler noticed, not that a test did.
            errors = [line for line in (build.stdout + build.stderr).splitlines() if "error" in line]
            first = errors[0].strip() if errors else "(no error line in the build output)"
            return None, f"the mutated tree did not build: {first}"
        # A mutant can make a test loop forever. ctest's default timeout is 1500 s,
        # so one such mutant cost 25 minutes and stalled pre_push.py; a timed-out
        # test fails, which is a kill, but a gate should not need half an hour to
        # say so. The slowest target here runs in seconds.
        result = subprocess.run(["ctest", "--test-dir", build_dir, "-R", rest, "-j",
                                 "--timeout", str(CTEST_TIMEOUT_SECONDS)],
                                capture_output=True, text=True, cwd=REPO)
        return result.returncode != 0, result.stdout[-800:]
    if kind == "tool":
        result = subprocess.run(rest.split(), capture_output=True, text=True, cwd=REPO)
        return result.returncode != 0, (result.stdout + result.stderr)[-800:]
    raise RuntimeError(f"unknown gate kind {kind!r}")


def check(mutants, build_dir, quiet=False):
    """`quiet` is for the self-test, whose fixtures are SUPPOSED to survive --
    their ::error:: lines would otherwise annotate a green job."""
    def say(message, error=False):
        if quiet:
            return
        print(f"::error::{message}" if error else message,
              file=sys.stderr if error else sys.stdout)

    survivors, broken = [], []

    for mutant in mutants:
        path = REPO / mutant["file"]
        original = None
        try:
            original = apply_mutant(mutant)
            killed, detail = run_gate(mutant["kills"], build_dir)
        finally:
            if original is not None:
                path.write_text(original, encoding="utf-8")
                force_rebuild(path)
                # Verified, not assumed. Leaving a mutant behind is worse than
                # having no tool at all.
                if path.read_text(encoding="utf-8") != original:
                    raise RuntimeError(
                        f"FAILED TO RESTORE {mutant['file']} after "
                        f"{mutant['name']} -- the working tree is MUTATED.")

        if killed is None:
            broken.append(f"{mutant['name']}: {detail}")
            say(f"  BROKEN  {mutant['name']:38} ({detail})")
        elif killed:
            say(f"  killed  {mutant['name']:38} by {mutant['kills']}")
        else:
            survivors.append(mutant)
            say(f"  SURVIVED {mutant['name']:37} {mutant['kills']} did not notice")

    # Leave the build directory consistent with the restored source. Without
    # this the last mutant's binaries stay on disk -- the tree reads clean and a
    # `ctest` in that directory fails for reasons nothing in the source explains.
    # Observed: 2 phantom failures out of 90 after a clean, all-killed run.
    if any(m["kills"].startswith("ctest:") for m in mutants):
        subprocess.run(["cmake", "--build", build_dir, "-j"],
                       capture_output=True, text=True, cwd=REPO)

    say(f"{len(mutants)} mutant(s), {len(survivors)} survivor(s), {len(broken)} broken")

    if broken:
        for entry in broken:
            say(entry, error=True)
    for mutant in survivors:
        say(f"{mutant['name']} SURVIVED. {mutant['why']} "
            f"{mutant['kills']} was expected to reject it and did not, so "
            f"that gate no longer covers this defect.", error=True)

    # A run over no mutants must not pass: same failure mode as every other
    # check here.
    if not mutants:
        say("no mutants selected -- a run over nothing reports success.", error=True)
        return 1

    return 1 if (survivors or broken) else 0


def self_test():
    """Prove the driver reports a survivor, and always restores the tree."""
    failures = []

    with tempfile.TemporaryDirectory() as tmp:
        fake = pathlib.Path(tmp) / "fake.hpp"
        fake.write_text("int marker = 1;\n")
        relative = fake.relative_to(fake.anchor)

        # A mutant whose gate does NOT reject it must be reported as a survivor.
        survivor = {
            "name": "fixture_survivor",
            "file": str(fake),
            "why": "fixture",
            "kills": "tool:true",  # `true` always exits 0 == did not reject
            "old": "int marker = 1;",
            "new": "int marker = 2;",
        }
        # `file` is joined against REPO, so point it at an absolute path instead.
        original_repo = globals()["REPO"]
        globals()["REPO"] = pathlib.Path(fake.anchor)
        try:
            code = check([dict(survivor, file=str(relative))], build_dir="unused", quiet=True)
            if code == 0:
                failures.append("a SURVIVING mutant was reported as success")
            if fake.read_text() != "int marker = 1;\n":
                failures.append("the fixture file was not restored")

            # A killed mutant passes.
            killed = dict(survivor, name="fixture_killed", kills="tool:false",
                          file=str(relative))
            if check([killed], build_dir="unused", quiet=True) != 0:
                failures.append("a KILLED mutant was reported as a failure")
            if fake.read_text() != "int marker = 1;\n":
                failures.append("the fixture file was not restored after a kill")

            # An anchor that no longer matches must be an error, not a silent skip.
            stale = dict(survivor, name="fixture_stale", old="int gone = 0;",
                         file=str(relative))
            try:
                check([stale], build_dir="unused", quiet=True)
                failures.append("a mutant whose anchor no longer matches was accepted")
            except RuntimeError:
                pass
            if fake.read_text() != "int marker = 1;\n":
                failures.append("the fixture file was not restored after a stale anchor")

            # An empty run must fail.
            if check([], build_dir="unused", quiet=True) == 0:
                failures.append("a run over NO mutants reported success")
        finally:
            globals()["REPO"] = original_repo

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: survivors, stale anchors and empty runs are "
          "rejected, killed mutants pass, and the tree is restored in every case")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--only", help="substring of a mutant name")
    parser.add_argument("--files", help="comma-separated paths: run only the mutants of these "
                                        "files (an empty list selects none)")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    selected = [m for m in MUTANTS if not args.only or args.only in m["name"]]
    if args.files is not None:
        wanted = {path for path in args.files.split(",") if path}
        selected = [m for m in selected if m["file"] in wanted]
        if not selected:
            print(f"no mutant targets {sorted(wanted) or 'any changed file'}; nothing to run")
            return 0

    if args.list:
        for mutant in selected:
            print(f"{mutant['name']:38} {mutant['kills']}")
        return 0

    if args.only and not selected:
        print(f"::error::--only {args.only!r} matched no mutant", file=sys.stderr)
        return 1

    if shutil.which("cmake") is None:
        print("::error::cmake not found", file=sys.stderr)
        return 1

    print(f"mutating the library and requiring the gates to notice "
          f"({len(selected)} mutant(s))")
    return check(selected, args.build_dir)


if __name__ == "__main__":
    sys.exit(main())
