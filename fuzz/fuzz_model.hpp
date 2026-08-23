// Reference models for the metl fuzz harnesses.
//
// WHY THIS EXISTS. Every harness under fuzz/ was, until this file, a
// MEMORY-SAFETY oracle and not a CORRECTNESS one. fuzz_flat_map.cpp says so in
// its own header comment: the operations are "bool/return-based (never
// assert)", and what it checks afterwards is sorted order, `size()`, and that
// `find` and `contains` agree with each other.
//
// All of that is true of a map that returns the wrong value. Shift every stored
// value by one slot and the keys stay sorted, the size stays right, find and
// contains still agree -- and 2,400 lines of harness, ASan, UBSan and every
// ClusterFuzzLite run come back clean. There was nothing anywhere in the
// repository that could tell a correct answer from a plausible one.
//
// A reference model closes that. After every operation the METL container is
// compared against a std:: container that is known to implement the same
// semantics, in BOTH directions -- every model entry must be findable in the
// container with the same value, and every container entry must be in the
// model. Together with equal sizes that is exact equality, not a spot check.
//
// ON THE HEAP, before anyone asks: std::map allocates, and this header is used
// ONLY by fuzz/, which builds and runs on the host. Invariant I1 ("no heap") is
// enforced by tools/check_invariants.py against a cross-compiled ARM image
// built from tests/embedded/invariant_probe.cpp, which does not include this
// file and never will. Nothing here relaxes I1; it is not in I1's subject.
//
// WHAT THE MODEL IS NOT. It is not a second implementation to test against on
// equal terms -- when the two disagree, std:: is right by definition and METL
// is the thing under test. The model's job is to be obviously correct, so it is
// written to be dull.

#ifndef METL_FUZZ_FUZZ_MODEL_HPP
#define METL_FUZZ_FUZZ_MODEL_HPP

#include <cstddef>
#include <map>

namespace metl_fuzz {

/// Trap on a mismatch, the way the harnesses already report a broken invariant.
/// libFuzzer records the abort as a crash and minimises the input that caused
/// it, so the failing opcode stream comes out of a failure for free.
inline void require(bool condition) noexcept {
  if (!condition) {
    __builtin_trap();
  }
}

/// Drives a fixed-capacity METL map and a std::map through the same operations,
/// comparing them after each one.
///
/// `Ordered` says whether the container's iteration order is part of its
/// contract. flat_map keeps its elements sorted and that IS the contract, so
/// its iteration is compared position by position against the model. An open-
/// addressed map has no defined order, so only the contents are compared --
/// asserting an order there would be testing the hash function, and the harness
/// would start failing for a change that broke nothing.
template <typename Map, typename Key, typename Value, bool Ordered>
class map_oracle {
 public:
  explicit map_oracle(Map& map) noexcept : map_(map) {}

  void try_emplace(Key key, Value value) {
    const bool absent = model_.find(key) == model_.end();
    const bool room = model_.size() < map_.capacity();
    const bool expected = absent && room;

    require(map_.try_emplace(Key{key}, Value{value}) == expected);
    if (expected) {
      model_.emplace(key, value);
    }
    verify();
  }

  void try_insert_or_assign(Key key, Value value) {
    const bool present = model_.find(key) != model_.end();
    const bool room = model_.size() < map_.capacity();
    // Assigning over an existing key always fits; only a NEW key can be
    // refused. Getting this backwards is exactly the kind of contract detail a
    // hand-written invariant check does not notice.
    const bool expected = present || room;

    require(map_.try_insert_or_assign(Key{key}, Value{value}) == expected);
    if (expected) {
      model_[key] = value;
    }
    verify();
  }

  void erase(Key key) {
    const bool expected = model_.erase(key) != 0;
    require(map_.erase(key) == expected);
    verify();
  }

  void lookup(Key key) const {
    const auto entry = model_.find(key);
    const Value* found = map_.find(key);

    require((found != nullptr) == (entry != model_.end()));
    require(map_.contains(key) == (entry != model_.end()));
    // The check the old harness could not make: not just THAT the key is there,
    // but that the value stored under it is the one that was put there.
    if (found != nullptr) {
      require(*found == entry->second);
    }
  }

  void clear() {
    map_.clear();
    model_.clear();
    verify();
  }

  std::size_t size() const noexcept { return model_.size(); }

  /// Full state comparison, in both directions.
  void verify() const {
    require(map_.size() == model_.size());
    require(map_.empty() == model_.empty());
    require(map_.size() <= map_.capacity());

    // Model -> container: everything that should be there is, with the right
    // value.
    for (const auto& entry : model_) {
      const Value* found = map_.find(entry.first);
      require(found != nullptr);
      require(*found == entry.second);
    }

    // Container -> model: nothing extra, and iteration agrees with lookup. With
    // equal sizes and both directions clean, the two are exactly equal.
    std::size_t seen = 0;
    auto expected = model_.begin();
    for (const auto& item : map_) {
      const auto entry = model_.find(item.key);
      require(entry != model_.end());
      require(item.value == entry->second);
      if (Ordered) {
        require(expected != model_.end());
        require(item.key == expected->first);
        require(item.value == expected->second);
        ++expected;
      }
      ++seen;
    }
    require(seen == model_.size());
  }

  const std::map<Key, Value>& model() const noexcept { return model_; }

 private:
  Map& map_;
  std::map<Key, Value> model_;
};

}  // namespace metl_fuzz

#endif  // METL_FUZZ_FUZZ_MODEL_HPP
