#pragma once

#ifndef __TEST_COMMON_H__
#define __TEST_COMMON_H__

#include "DEFS_WIN32_NO_BS.h"
#include "minunit.h"

#include <ht_error.h>
#include <setjmp.h>

// NOTE: passes if HT_ASSERT fired, fails if it did not
#define MU_ASSERT_FIRES( expr )                 \
    do {                                        \
        gHtAssertFired = 0;                     \
        if( !setjmp( gHtAssertJmpbuf ) )        \
        {                                       \
            ( expr );                           \
        }                                       \
        mu_check( gHtAssertFired );             \
    } while( 0 )

#endif // !__TEST_COMMON_H__
