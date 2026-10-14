- **A namespace-scope `object_pool` is constant-initialized since C++20
  (#394).** Its default constructor was declared `constexpr` but left the slot
  storage uninitialized, so no pool could be constant-initialized: a global one
  was built at startup, in link order, and `constinit` on one failed to
  compile. It now zeroes the storage during constant evaluation only, as the
  containers do, so a local pool still stores nothing to it.
