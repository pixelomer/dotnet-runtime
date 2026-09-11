// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

/*++



Module Name:

    common.c

Abstract:

    Implementation of the common mapping functions.



--*/

#include "pal/palinternal.h"
#include "pal/dbgmsg.h"

#include "common.h"

#include "pal/mapnative.h"

SET_DEFAULT_DEBUG_CHANNEL(VIRTUAL);

/*****
 *
 * W32toUnixAccessControl( DWORD ) - Maps Win32 to Unix memory access controls .
 *
 */
INT W32toUnixAccessControl( IN DWORD flProtect )
{
    INT MemAccessControl = 0;

    switch ( flProtect & 0xff )
    {
    case PAGE_READONLY :
        MemAccessControl = MapRead;
        break;
    case PAGE_READWRITE :
        MemAccessControl = MapRead | MapWrite;
        break;
    case PAGE_EXECUTE_READWRITE:
        MemAccessControl = MapExecute | MapRead | MapWrite;
        break;
    case PAGE_EXECUTE :
        MemAccessControl = MapExecute;
        break;
    case PAGE_EXECUTE_READ :
        MemAccessControl = MapExecute | MapRead;
        break;
    case PAGE_NOACCESS :
        MemAccessControl = MapNone;
        break;

    default:
        MemAccessControl = 0;
        ERROR( "Incorrect or no protection flags specified.\n" );
        break;
    }
    return MemAccessControl;
}
