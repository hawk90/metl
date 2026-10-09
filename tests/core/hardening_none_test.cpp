// METL_HARDENING_NONE: only METL_HARDEN survives; METL_ASSERT and METL_DASSERT
// are both stripped.
#undef METL_HARDENING  // this test pins its own level, whatever the build passes
#define METL_HARDENING 0
#include "hardening_common.h"
