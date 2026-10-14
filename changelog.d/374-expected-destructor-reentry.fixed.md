- **`expected` no longer destroys a member twice when the member's destructor
  changes it (#374).** Switching between a value and an error destroyed the old
  member while `has_value()` still named it, so a destructor that reached back
  into the same `expected` and changed it destroyed the old member again. The
  state now reads "switching" while a member is being destroyed, by any path,
  and a change, copy or move made from that destructor fails `METL_HARDEN`.
  This precondition is stated on both templates. `sizeof` is unchanged.
