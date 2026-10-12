"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "event_dispatcher_calls_mid_dispatch_subscriber",
    "file": "include/metl/event_dispatcher.hpp",
    "why": "a listener subscribed during dispatch hears the current event "
           "or not depending on the slot it lands in.",
    "kills": "ctest:event_dispatcher_reentrancy",
    "old": "    const std::uint64_t first_unheard = next_id_;",
    "new": "    const std::uint64_t first_unheard = ~std::uint64_t{0};",
}
