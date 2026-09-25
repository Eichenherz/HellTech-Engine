#pragma once

#ifndef __TEST_COMMON_H__
#define __TEST_COMMON_H__

#include <System/Win32/DEFS_WIN32_NO_BS.h>
#include "minunit.h"

#include <ht_error.h>

// NOTE: passes if HT_ASSERT fired, fails if it did not
#define MU_ASSERT_FIRES( expr )                                                     \
    do {                                                                            \
        bool assertFired = false;                                                   \
        try { ( expr ); } catch( const ht_assert_fired& ) { assertFired = true; }   \
        mu_check( assertFired );                                                    \
    } while( 0 )

#endif // !__TEST_COMMON_H__
