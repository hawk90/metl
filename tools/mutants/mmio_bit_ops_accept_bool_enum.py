"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "mmio_bit_ops_accept_bool_enum",
    "file": "include/metl/mmio.hpp",
    "why": "mmio_ptr::clear_bits tests the enum instead of its underlying type, so "
           "a bool-backed enum register compiles and the clear leaves it at 1 (#576).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": "  void clear_bits(T bits) const noexcept {\n"
           "    static_assert(!std::is_same_v<std::remove_cv_t<detail::mmio_bits_t<T>>, bool>,",
    "new": "  void clear_bits(T bits) const noexcept {\n"
           "    static_assert(!std::is_same_v<std::remove_cv_t<T>, bool>,",
}
