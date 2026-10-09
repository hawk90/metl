// METL_HARDENING_FAST (the release default): METL_ASSERT + METL_HARDEN fire,
// METL_DASSERT is stripped.
#undef METL_HARDENING  // this test pins its own level, whatever the build passes
#define METL_HARDENING 1
#include "hardening_common.h"
