// METL_HARDENING_DEBUG (the debug default): all three tiers fire.
#undef METL_HARDENING  // this test pins its own level, whatever the build passes
#define METL_HARDENING 2
#include "hardening_common.h"
