/*++

Copyright (c) 1993  Microsoft Corporation

Module Name:

    maptest.h

Abstract:

    Win32 test app for the MAPMEM driver

Environment:

    User mode

Notes:


Revision History:

--*/


#include "windows.h"
#include "winioctl.h"
#include "stdio.h"
#include "stdlib.h"
#include "ioaccess.h"

typedef LARGE_INTEGER PHYSICAL_ADDRESS;

#include "3dfx/cvgregs.h"
#include "3dfx/cvgdefs.h"

//
// Define the various device type values.  Note that values used by Microsoft
// Corporation are in the range 0-32767, and 32768-65535 are reserved for use
// by customers.
//

#define FILE_DEVICE_MAPMEM  0x00008000



//
// Macro definition for defining IOCTL and FSCTL function control codes.  Note
// that function codes 0-2047 are reserved for Microsoft Corporation, and
// 2048-4095 are reserved for customers.
//

#define MAPMEM_IOCTL_INDEX  0x800

#define IOCTL_MAPMEM_MAP_USER_PHYSICAL_MEMORY   CTL_CODE(FILE_DEVICE_MAPMEM , \
    MAPMEM_IOCTL_INDEX,  \
    METHOD_BUFFERED,     \
    FILE_ANY_ACCESS)

#define IOCTL_MAPMEM_UNMAP_USER_PHYSICAL_MEMORY CTL_CODE(FILE_DEVICE_MAPMEM,  \
    MAPMEM_IOCTL_INDEX+1,\
    METHOD_BUFFERED,     \
    FILE_ANY_ACCESS)


int
__cdecl
main(
    IN int  argc,
    IN char *argv[])
/*++

Routine Description:

    Tries to open the MAPMEM driver & send it a couple of IOCTLs.

Arguments:

    argc - count of command line arguments

    argv - command line arguments

Return Value:


--*/
{
    HANDLE               hDriver;
    PVOID                pPartyMem;
    DWORD                cbReturned;


    //
    // Try to open the device
    //

    if ((hDriver = CreateFileA("\\\\.\\MAPMEM",
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              NULL,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL
                              )) != ((HANDLE)-1))

        printf ("\nRetrieved valid handle for MAPMEM driver\n");


    else
    {
        printf ("Can't get a handle to MAPMEM driver\n");

        return 0;
    }



    //
    // Try to map the memory
    //

    if (DeviceIoControl (hDriver,
                         (DWORD) IOCTL_MAPMEM_MAP_USER_PHYSICAL_MEMORY,
                         0,
                         0,
                         &pPartyMem,
                         sizeof(PVOID),
                         &cbReturned,
                         0
                         ) )
    {
        ULONG j;

        //
        // party on memory...
        //

        if (pPartyMem)
        {
            SstRegs* sst = (SstRegs*) pPartyMem;
            
            printf("pPartyMem: 0x%x\n", pPartyMem);
            printf("SST Status: %d\n", sst->status);
            // printf("SLI: %d\n", sst->fbiInit1 & SST_EN_SCANLINE_INTERLEAVE);

            //
            // Unmap the memory
            //

            /*DeviceIoControl (hDriver,
                             (DWORD) IOCTL_MAPMEM_UNMAP_USER_PHYSICAL_MEMORY,
                             &pPartyMem,
                             sizeof(PVOID),
                             NULL,
                             0,
                             &cbReturned,
                             0
                             );*/
        }

        else

            printf ("pPartyMem = NULL\n");
    }

    else

        //
        // We failed to map, possibly due to resource conflicts (i.e
        // some other driver/device already grabbed the section we
        // wanted).
        //

        printf ("DeviceIoControl failed\n");


    CloseHandle(hDriver);

    return 1;
}
