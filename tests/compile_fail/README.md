# Compile-failure cases

METL's public headers carry 17 user-facing `static_assert`s -- "Capacity must be
power of two", "variant_alternative index out of range", "get_if<T> requires
unique alternative type". For a template library those messages **are** the
error handling: they are what a caller sees when they misuse the API, and they
are the only thing standing between a misuse and whatever the compiler would
otherwise say about a 40-line instantiation stack.

Not one of them was verified to fire.

A `static_assert` whose condition is accidentally always true is invisible. It
compiles, it never complains, and the contract it claims to enforce quietly
stops being enforced -- the same shape as a CI step that can only pass, which
this repository has spent a long time removing everywhere else. `--self-test`
canaries prove the *checkers* still bite; nothing proved the *contracts* did.

## How a case works

Each `.cpp` here is compiled TWICE.

    without METL_COMPILE_FAIL   must succeed
    with    METL_COMPILE_FAIL   must fail, with the message on the EXPECT-ERROR line

The first compile is the part that matters and the part a plain
`WILL_FAIL`-style test does not do. A file that fails to compile because of a
typo, a missing include, or a renamed header "passes" a check that only looks
for a non-zero exit -- and it passes forever, testing nothing. Requiring the
same file to build cleanly with the offending construct removed proves the
failure comes from the line under test.

Matching the message matters for the same reason: an assertion can start firing
for a different reason than the one it was written for, and a check that only
counts errors would not notice.

## Adding one

    // EXPECT-ERROR: <substring of the diagnostic, usually the assert message>
    //
    // <what contract this pins, and why it is worth pinning>

    #include <metl/thing.hpp>

    // Valid form -- this is the control.
    metl::thing<int, 8> ok;

    #ifdef METL_COMPILE_FAIL
    metl::thing<int, 7> bad;
    #endif

`tools/check_compile_fail.py` finds every `*.cpp` here automatically; there is
no list to keep in step.
