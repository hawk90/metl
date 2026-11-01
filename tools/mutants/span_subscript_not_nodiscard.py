"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "span_subscript_not_nodiscard",
    "file": "include/metl/span.hpp",
    "why": "a span subscript whose result is dropped compiles silently, unlike "
           "every other sequence container's operator[].",
    "kills": "tool:tools/check_api_contract.py",
    "old": "METL_NODISCARD constexpr reference operator[](size_type index) const noexcept {",
    "new": "constexpr reference operator[](size_type index) const noexcept {",
}
