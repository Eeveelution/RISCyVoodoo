/*++

Copyright (c) 1993  Microsoft Corporation

Module Name:

    mapmem.c

Abstract:

    A simple driver sample which shows how to map physical memory
    into a user-mode process's adrress space using the
    Zw*MapViewOfSection APIs.

Environment:

    kernel mode only

Notes:

    For the sake of simplicity this sample does not attempt to
    recognize resource conflicts with other drivers/devices. A
    real-world driver would call IoReportResource usage to
    determine whether or not the resource is available, and if
    so, register the resource under it's name.

Revision History:

--*/


#include "ntddk.h"
#include "mapmem.h"
#include "stdarg.h"


#define _M_ALPHA
#define _ALPHA_

#include <stdio.h>

NTSTATUS
MapMemDispatch(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    );

VOID
MapMemUnload(
    IN PDRIVER_OBJECT DriverObject
    );

NTSTATUS
MapMemMapTheMemory(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PVOID      ioBuffer,
    IN ULONG          inputBufferLength,
    IN ULONG          outputBufferLength
    );



#define MapMemKdPrint(arg) DbgPrint arg

FxU32 __stdcall sst1InitRead32(FxU32 *addr)
{
    return(*addr);
}



// # define P6FENCE asm volatile("mb" ::: "memory");

#define SET(d, s)    ((*d) = s)

void sst1InitWrite32(FxU32 *addr, FxU32 data)
{
  /* If the client software is using the command fifo then they are
   * responsible for passing a callback that can be used to put
   * register writes from the init code into the command fifo that
   * they are managing. However, some registers cannot be accessed via
   * the command fifo, and, inconveniently, these are not contiguously
   * allocated.  
   */
//   const FxU32 addrOffset = ((const FxU32)addr - (const FxU32)sst1CurrentBoard->virtAddr[0]);
//   FxBool directWriteP = ((sst1CurrentBoard == NULL) ||
//                          (sst1CurrentBoard->set32 == NULL) ||
//                          sst1CurrentBoard->fbiLfbLocked ||
//                          (addrOffset == 0x004) ||                            /* intrCtrl */
//                          ((addrOffset >= 0x1E0) && (addrOffset <= 0x200)) || /* cmdFifoBase ... fbiInit4 */
//                          ((addrOffset >= 0x208) && (addrOffset <= 0x224)) || /* backPorch ... vSync */
//                          ((addrOffset >= 0x22C) && (addrOffset <= 0x23C)) || /* dacData ... borderColor */
//                          ((addrOffset >= 0x244) && (addrOffset <= 0x24C)));  /* fbiInit5 ... fbiInit7 */

//   if (directWriteP) {
    // __asm {
    //     mb
    // }
    SET(addr, data);
    // __asm {
    //     mb
    // }
//   } else {
//     (*sst1CurrentBoard->set32)(addr, data);
//   }
}

#define IGET(A)    sst1InitRead32((FxU32 *) &(A))
#define ISET(A,D)  sst1InitWrite32((FxU32 *) &(A), D)  

void DetectVoodooCvgCard(ULONG *outBus, ULONG *deviceNum, USHORT *deviceId) {
    PCI_SLOT_NUMBER SlotNumber;
	PPCI_COMMON_CONFIG PciData;
	UCHAR buffer[PCI_COMMON_HDR_LENGTH];
	ULONG i, f, j, bus, k;
	BOOLEAN flag = TRUE;
	
	PciData = (PPCI_COMMON_CONFIG) buffer;
	SlotNumber.u.bits.Reserved = 0;

    for (bus = 0; ; bus++) {
		for (i = 0; i < PCI_MAX_DEVICES; i++) {
            DbgPrint("Bus %d Device %d\n", bus, i);

			SlotNumber.u.bits.DeviceNumber = i;
			// mio controllers only use function 0
			SlotNumber.u.bits.FunctionNumber = 0;
			
			j = HalGetBusData(
				PCIConfiguration,
				bus,
				SlotNumber.u.AsULONG,
				PciData,
				PCI_COMMON_HDR_LENGTH
			);

            if(j == 0) {
                goto end;
            }
			
			if(PciData == NULL) {
                continue;
            }

            if(PciData->VendorID == 0x121a) {
                DbgPrint("Bus: %d; Device Number: %d FOUND 3DFX CARD: %d\n", bus, i, PciData->DeviceID);

                for(k = 0; k != 6; k++) {
                    DbgPrint("BaseAddresses[%d] = 0x%x\n", k, PciData->u.type0.BaseAddresses[k]);
                }

                *outBus = bus;
                *deviceNum = i;
                *deviceId = PciData->DeviceID;
            }
		}
	}

    end:;
}

int sst1InitIdleFBINoNOP(SstRegs *sst)
{
    FxU32 cntr;

    if(!sst)
        return(0);

    // ISET(sst->nopCMD, 0x0);
    cntr = 0;
    while(1) {
        if(!(IGET(sst->status) & SST_FBI_BUSY)) {
            if(++cntr > 5)
                break;
        } else
            cntr = 0;
    }
    return(1);
}


static void* VOODOOPTR;
static void* MapRegisterBase;   


void PrintVoodooStatus(ULONG status) {
    DbgPrint("VRETRACE: %d | ", status & SST_VRETRACE);
    DbgPrint("FBI_BUSY: %d | ", status & SST_FBI_BUSY);
    DbgPrint("TMU_BUSY: %d | ", status & SST_TMU_BUSY);
    DbgPrint("SST_BUSY: %d | ", status & SST_TREX_BUSY);
    DbgPrint("FIFO_LVL: %d\n", status & SST_FIFOLEVEL);
}

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT  DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:

    Installable driver initialization entry point.
    This entry point is called directly by the I/O system.

Arguments:

    DriverObject - pointer to the driver object

    RegistryPath - pointer to a unicode string representing the path
                   to driver-specific key in the registry

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise

--*/
{

    PDEVICE_OBJECT deviceObject = NULL;
    NTSTATUS       ntStatus;
    WCHAR          deviceNameBuffer[] = L"\\Device\\MapMem";
    UNICODE_STRING deviceNameUnicodeString;
    WCHAR          deviceLinkBuffer[] = L"\\DosDevices\\MAPMEM";
    UNICODE_STRING deviceLinkUnicodeString;
    ULONG voodooBus, voodooDeviceNum;
    USHORT voodooDeviceId;
    PCM_RESOURCE_LIST allocatedResources;
    
    
   
    //
    // Create an EXCLUSIVE device object (only 1 thread at a time
    // can make requests to this device)
    //

    RtlInitUnicodeString (&deviceNameUnicodeString,
                          deviceNameBuffer);

    ntStatus = IoCreateDevice (DriverObject,
                               0,
                               &deviceNameUnicodeString,
                               FILE_DEVICE_MAPMEM,
                               0,
                               TRUE,
                               &deviceObject
                               );

    if (NT_SUCCESS(ntStatus))
    {
        ULONG voodooBus, voodooDeviceNum;
        USHORT voodooDeviceId;
        PCI_SLOT_NUMBER slotNumber;
        PCI_COMMON_CONFIG pciConfig;
        ULONG i;
        PCM_RESOURCE_LIST allocatedResources;
        ULONG isInIoSpace = 4;
        PHYSICAL_ADDRESS voodooPhysAddr;
        PVOID mappedAddress;
        //
        // Create dispatch points for device control, create, close.
        //

        DriverObject->MajorFunction[IRP_MJ_CREATE]         =
        DriverObject->MajorFunction[IRP_MJ_CLOSE]          =
        DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = MapMemDispatch;
        DriverObject->DriverUnload                         = MapMemUnload;
        
        //
        // Create a symbolic link, e.g. a name that a Win32 app can specify
        // to open the device
        //

        RtlInitUnicodeString(&deviceLinkUnicodeString,
                             deviceLinkBuffer);

        ntStatus = IoCreateSymbolicLink (&deviceLinkUnicodeString,
                                         &deviceNameUnicodeString);

        if (!NT_SUCCESS(ntStatus))
        {
            //
            // Symbolic link creation failed- note this & then delete the
            // device object (it's useless if a Win32 app can't get at it).
            //

            MapMemKdPrint (("MAPMEM.SYS: IoCreateSymbolicLink failed\n"));

            IoDeleteDevice (deviceObject);
        }

        DetectVoodooCvgCard(&voodooBus, &voodooDeviceNum, &voodooDeviceId);

        slotNumber.u.bits.DeviceNumber = voodooDeviceNum;
        slotNumber.u.bits.FunctionNumber = 0;
        slotNumber.u.bits.Reserved = 0;

        ntStatus = HalAssignSlotResources(RegistryPath, NULL, DriverObject, deviceObject, PCIBus, voodooBus, voodooDeviceNum, &allocatedResources);

        if(!NT_SUCCESS(ntStatus)) {
            DbgPrint("HalAssignSlotResources failed\n");

            return;
        }

        DbgPrint("Assigned Resources: %d\n", allocatedResources->Count);

        if(allocatedResources->Count == 0) {
            return;
        }
        
        DbgPrint("Voodoo Card Configured at: Low Part: 0x%x High Part: 0x%x\n", 
            allocatedResources->List[0].PartialResourceList.PartialDescriptors[0].u.Memory.Start.LowPart, 
            allocatedResources->List[0].PartialResourceList.PartialDescriptors[0].u.Memory.Start.HighPart
        );

        i = HalGetBusData(PCIConfiguration, voodooBus, slotNumber.u.AsULONG, &pciConfig, PCI_COMMON_HDR_LENGTH);

        DbgPrint("HalGetBusData: %d\n", i);

        pciConfig.LatencyTimer = 0xFF;
        pciConfig.Command = 0x0006; // bus master & memory space enable

        i = HalSetBusData(PCIConfiguration, voodooBus, slotNumber.u.AsULONG, &pciConfig, PCI_COMMON_HDR_LENGTH);

        DbgPrint("HalSetBusData: %d\n", i);

        ntStatus = HalTranslateBusAddress(PCIBus, voodooBus, allocatedResources->List[0].PartialResourceList.PartialDescriptors[0].u.Memory.Start, &isInIoSpace, &voodooPhysAddr);

        if(!ntStatus) {
            DbgPrint("HalTranslateBusAddress failed.\n");

            return;
        }

        DbgPrint("Voodoo physical address: 0x%x; IsInIoSpace: %d\n", voodooPhysAddr.LowPart, isInIoSpace);

        // returns virtual address
        mappedAddress = MmMapIoSpace(voodooPhysAddr, 16 * 1024 * 1024, MmNonCached);

        DbgPrint("Mapped address: 0x%x\n", mappedAddress);

        if(mappedAddress == NULL) {
            return;
        }

        {
            ULONG isInIoSpace1 = 4;
            ULONG isInIoSpace2 = 4;
            PHYSICAL_ADDRESS basePlusLength;
            BOOLEAN successBase, successEnd;

            PHYSICAL_ADDRESS baseAddr, endAddr;

            PVOID virtualAddress;

            OBJECT_ATTRIBUTES  objectAttributes;
            HANDLE             physicalMemoryHandle  = NULL;
            PVOID              PhysicalMemorySection = NULL;

            ULONG Length = 16 * 1024 * 1024;

            basePlusLength.QuadPart = voodooPhysAddr.QuadPart + (16 * 1024 * 1024); 

            successBase = HalTranslateBusAddress(PCIBus, voodooBus, voodooPhysAddr, &isInIoSpace1, &baseAddr);
            successEnd = HalTranslateBusAddress(PCIBus, voodooBus, basePlusLength, &isInIoSpace2, &endAddr);

            if(!successBase) {
                DbgPrint("Translating base address failed.\n");

                return;
            }

            if(!successEnd) {
                DbgPrint("Translating end address failed.\n");

                return;
            }

            DbgPrint("baseAddr: 0x%x\n", baseAddr.LowPart);
            DbgPrint("endAddr: 0x%x\n", endAddr.LowPart);

// #define STALL_STATUS() DbgPrint("Status: 0x%x", IGET(sst->status)); DbgPrint("Status: 0x%x", IGET(sst->status)); DbgPrint("Status: 0x%x", IGET(sst->status))
#define STALL_STATUS() PrintVoodooStatus(IGET(sst->status)); PrintVoodooStatus(IGET(sst->status)); PrintVoodooStatus(IGET(sst->status))
            {
                SstRegs* sst = (SstRegs*) mappedAddress;
                ULONG treshold = 0x8;
                ULONG clockDelay = 0x8;


                if(IGET(sst->fbiInit1) & SST_EN_SCANLINE_INTERLEAVE) {
                    DbgPrint("SLI active!\n");
                } else {
                    DbgPrint("SLI not active!\n");
                }

                //Enable writes to FBIINIT registers
                //Do not allow writes into the pci fifo until everything is reset
                DbgPrint("Enabling writes to FBIINIT and reseting bus snooping\n");
                {
                    ULONG initWriteEnable = BIT(0);
                    ULONG snoopDefault = 0;

                    i = HalSetBusDataByOffset(PCIConfiguration, voodooBus, slotNumber.u.AsULONG, &initWriteEnable, 0x40, 4); DbgPrint("Init Write: %d\n", i);
                    i = HalSetBusDataByOffset(PCIConfiguration, voodooBus, slotNumber.u.AsULONG, &snoopDefault, 0x44, 4); DbgPrint("Snoop0: %d\n", i);
                    i = HalSetBusDataByOffset(PCIConfiguration, voodooBus, slotNumber.u.AsULONG, &snoopDefault, 0x48, 4); DbgPrint("Snoop1: %d\n", i);

                    STALL_STATUS();
                }

                //Adjust TREX to FBI FIFO
                {
                    DbgPrint("fbiInit3 happening\n");

                    ISET(sst->fbiInit3,
                        (SST_FBIINIT3_DEFAULT & ~(SST_FT_CLK_DEL_ADJ | SST_TF_FIFO_THRESH)) |
                        (clockDelay << SST_FT_CLK_DEL_ADJ_SHIFT) |
                        (treshold << SST_TF_FIFO_THRESH_SHIFT)
                    );

                    STALL_STATUS();
                }

                DbgPrint("Resetting graphics and video units\n");

                ISET(sst->fbiInit1, IGET(sst->fbiInit1) | SST_VIDEO_RESET);

                STALL_STATUS();
                
                DbgPrint("fbiInit0\n");

                ISET(sst->fbiInit0, IGET(sst->fbiInit0) | (SST_GRX_RESET | SST_PCI_FIFO_RESET));

                DbgPrint("waiting...\n");

                sst1InitIdleFBINoNOP(sst);

                DbgPrint("unresetting fbiInit0\n");

                ISET(sst->fbiInit0, IGET(sst->fbiInit0) & ~SST_GRX_RESET);
                sst1InitIdleFBINoNOP(sst);

                DbgPrint("Resetting fbi registers\n");
                // Reset all FBI and TREX Init registers
                ISET(sst->fbiInit0, SST_FBIINIT0_DEFAULT);
                ISET(sst->fbiInit1, SST_FBIINIT1_DEFAULT);
                ISET(sst->fbiInit2, SST_FBIINIT2_DEFAULT);
                ISET(sst->fbiInit3,
                (SST_FBIINIT3_DEFAULT & ~(SST_FT_CLK_DEL_ADJ | SST_TF_FIFO_THRESH)) |
                (clockDelay << SST_FT_CLK_DEL_ADJ_SHIFT) |
                (treshold << SST_TF_FIFO_THRESH_SHIFT));
                ISET(sst->fbiInit4, SST_FBIINIT4_DEFAULT);
                ISET(sst->fbiInit5, SST_FBIINIT5_DEFAULT);
                ISET(sst->fbiInit6, SST_FBIINIT6_DEFAULT);
                ISET(sst->fbiInit7, SST_FBIINIT7_DEFAULT);
                sst1InitIdleFBINoNOP(sst);  // Wait until init regs are reset

                //enable writes and pushes to fifo
                {
                    ULONG cfg = SST_INITWR_EN | SST_PCI_FIFOWR_EN;

                    HalSetBusDataByOffset(PCIConfiguration, voodooBus, slotNumber.u.AsULONG, &cfg, 0x40, 4);
                }
            }

             
            // DbgPrint("SST Status: %d\n", ((SstRegs*)mappedAddress)->init);

            // ntStatus = ZwOpenSection (&physicalMemoryHandle,
            //                   SECTION_ALL_ACCESS,
            //                   &objectAttributes);

            // if (!NT_SUCCESS(ntStatus))
            // {
            //     MapMemKdPrint (("MAPMEM.SYS: ZwOpenSection failed\n"));

            //     return;
            // }

            // ntStatus = ObReferenceObjectByHandle (physicalMemoryHandle,
            //                                     SECTION_ALL_ACCESS,
            //                                     (POBJECT_TYPE) NULL,
            //                                     KernelMode,
            //                                     &PhysicalMemorySection,
            //                                     (POBJECT_HANDLE_INFORMATION) NULL);

            // if (!NT_SUCCESS(ntStatus))
            // {
            //     MapMemKdPrint (("MAPMEM.SYS: ObReferenceObjectByHandle failed\n"));

            //     return;
            // }

            // ntStatus = ZwMapViewOfSection(physicalMemoryHandle,
            //                           NtCurrentProcess(),
            //                           &mappedAddress,
            //                           0L,
            //                           Length,
            //                           &baseAddr,
            //                           &Length,
            //                           ViewUnmap,
            //                           0,
            //                           PAGE_READWRITE | PAGE_NOCACHE);

            // ZwClose(physicalMemoryHandle);
        }
    }
    else
    {
        MapMemKdPrint (("MAPMEM.SYS: IoCreateDevice failed\n"));
    }

    return ntStatus;
}



NTSTATUS
MapMemDispatch(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )
/*++

Routine Description:

    Process the IRPs sent to this device.

Arguments:

    DeviceObject - pointer to a device object

    Irp          - pointer to an I/O Request Packet

Return Value:


--*/
{
    PIO_STACK_LOCATION irpStack;
    PVOID              ioBuffer;
    ULONG              inputBufferLength;
    ULONG              outputBufferLength;
    ULONG              ioControlCode;
    NTSTATUS           ntStatus;


    //
    // Init to default settings- we only expect 1 type of
    //     IOCTL to roll through here, all others an error.
    //

    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;


    //
    // Get a pointer to the current location in the Irp. This is where
    //     the function codes and parameters are located.
    //

    irpStack = IoGetCurrentIrpStackLocation(Irp);


    //
    // Get the pointer to the input/output buffer and it's length
    //

    ioBuffer           = Irp->AssociatedIrp.SystemBuffer;
    inputBufferLength  = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    outputBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;


    switch (irpStack->MajorFunction)
    {
    case IRP_MJ_CREATE:

        MapMemKdPrint (("MAPMEM.SYS: IRP_MJ_CREATE\n"));

        break;



    case IRP_MJ_CLOSE:

        MapMemKdPrint (("MAPMEM.SYS: IRP_MJ_CLOSE\n"));

        break;



    case IRP_MJ_DEVICE_CONTROL:

        ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;

        switch (ioControlCode)
        {
        case IOCTL_MAPMEM_MAP_USER_PHYSICAL_MEMORY:

            Irp->IoStatus.Status = MapMemMapTheMemory (DeviceObject,
                                                       ioBuffer,
                                                       inputBufferLength,
                                                       outputBufferLength
                                                       );

            if (NT_SUCCESS(Irp->IoStatus.Status))
            {
                //
                // Success! Set the following to sizeof(PVOID) to
                //     indicate we're passing valid data back.
                //

                Irp->IoStatus.Information = sizeof(PVOID);

                MapMemKdPrint (("MAPMEM.SYS: memory successfully mapped\n"));
            }

            else
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;

                MapMemKdPrint (("MAPMEM.SYS: memory map failed :(\n"));
            }

            break;



        case IOCTL_MAPMEM_UNMAP_USER_PHYSICAL_MEMORY:

            if (inputBufferLength >= sizeof(PVOID))
            {
                Irp->IoStatus.Status = ZwUnmapViewOfSection ((HANDLE) -1,
                                                             *((PVOID *) ioBuffer)
                                                             );

                MapMemKdPrint (("MAPMEM.SYS: memory successfully unmapped\n"));
            }
            else
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;

                MapMemKdPrint (("MAPMEM.SYS: ZwUnmapViewOfSection failed\n"));
            }

            break;



        default:

            MapMemKdPrint (("MAPMEM.SYS: unknown IRP_MJ_DEVICE_CONTROL\n"));

            Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;

            break;

        }

        break;
    }


    //
    // DON'T get cute and try to use the status field of
    // the irp in the return status.  That IRP IS GONE as
    // soon as you call IoCompleteRequest.
    //

    ntStatus = Irp->IoStatus.Status;

    IoCompleteRequest(Irp,
                      IO_NO_INCREMENT);


    //
    // We never have pending operation so always return the status code.
    //

    return ntStatus;
}



VOID
MapMemUnload(
    IN PDRIVER_OBJECT DriverObject
    )
/*++

Routine Description:

    Just delete the associated device & return.

Arguments:

    DriverObject - pointer to a driver object

Return Value:


--*/
{
    WCHAR                  deviceLinkBuffer[]  = L"\\DosDevices\\MAPMEM";
    UNICODE_STRING         deviceLinkUnicodeString;



    //
    // Free any resources
    //



    //
    // Delete the symbolic link
    //

    RtlInitUnicodeString (&deviceLinkUnicodeString,
                          deviceLinkBuffer
                          );

    IoDeleteSymbolicLink (&deviceLinkUnicodeString);



    //
    // Delete the device object
    //

    MapMemKdPrint (("MAPMEM.SYS: unloading\n"));

    IoDeleteDevice (DriverObject->DeviceObject);
}



NTSTATUS
MapMemMapTheMemory(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PVOID      IoBuffer,
    IN ULONG          InputBufferLength,
    IN ULONG          OutputBufferLength
    )
/*++

Routine Description:

    Given a physical address, maps this address into a user mode process's
    address space

Arguments:

    DeviceObject       - pointer to a device object

    IoBuffer           - pointer to the I/O buffer

    InputBufferLength  - input buffer length

    OutputBufferLength - output buffer length

Return Value:

    STATUS_SUCCESS if sucessful, otherwise
    STATUS_UNSUCCESSFUL,
    STATUS_INSUFFICIENT_RESOURCES,
    (other STATUS_* as returned by kernel APIs)

--*/
{

    PPHYSICAL_MEMORY_INFO ppmi = (PPHYSICAL_MEMORY_INFO) IoBuffer;

    INTERFACE_TYPE     interfaceType;
    ULONG              busNumber;
    PHYSICAL_ADDRESS   physicalAddress;
    ULONG              length;
    UNICODE_STRING     physicalMemoryUnicodeString;
    OBJECT_ATTRIBUTES  objectAttributes;
    HANDLE             physicalMemoryHandle  = NULL;
    PVOID              PhysicalMemorySection = NULL;
    ULONG              inIoSpace, inIoSpace2;
    NTSTATUS           ntStatus;
    PHYSICAL_ADDRESS   physicalAddressBase;
    PHYSICAL_ADDRESS   physicalAddressEnd;
    PHYSICAL_ADDRESS   viewBase;
    PHYSICAL_ADDRESS   mappedLength;
    BOOLEAN            translateBaseAddress;
    BOOLEAN            translateEndAddress;
    PVOID              virtualAddress;



    if ( ( InputBufferLength  < sizeof (PHYSICAL_MEMORY_INFO) ) ||
         ( OutputBufferLength < sizeof (PVOID) ) )
    {
       MapMemKdPrint (("MAPMEM.SYS: Insufficient input or output buffer\n"));

       ntStatus = STATUS_INSUFFICIENT_RESOURCES;

       goto done;
    }

    interfaceType          = ppmi->InterfaceType;
    busNumber              = ppmi->BusNumber;
    physicalAddress        = ppmi->BusAddress;
    inIoSpace = inIoSpace2 = ppmi->AddressSpace;
    length                 = ppmi->Length;

    *((PVOID *) IoBuffer) = VOODOOPTR;

    return;


    //
    // Get a pointer to physical memory...
    //
    // - Create the name
    // - Initialize the data to find the object
    // - Open a handle to the oject and check the status
    // - Get a pointer to the object
    // - Free the handle
    //

    RtlInitUnicodeString (&physicalMemoryUnicodeString,
                          L"\\Device\\PhysicalMemory");

    InitializeObjectAttributes (&objectAttributes,
                                &physicalMemoryUnicodeString,
                                OBJ_CASE_INSENSITIVE,
                                (HANDLE) NULL,
                                (PSECURITY_DESCRIPTOR) NULL);

    ntStatus = ZwOpenSection (&physicalMemoryHandle,
                              SECTION_ALL_ACCESS,
                              &objectAttributes);

    if (!NT_SUCCESS(ntStatus))
    {
        MapMemKdPrint (("MAPMEM.SYS: ZwOpenSection failed\n"));

        goto done;
    }

    ntStatus = ObReferenceObjectByHandle (physicalMemoryHandle,
                                          SECTION_ALL_ACCESS,
                                          (POBJECT_TYPE) NULL,
                                          KernelMode,
                                          &PhysicalMemorySection,
                                          (POBJECT_HANDLE_INFORMATION) NULL);

    if (!NT_SUCCESS(ntStatus))
    {
        MapMemKdPrint (("MAPMEM.SYS: ObReferenceObjectByHandle failed\n"));

        goto close_handle;
    }


    //
    // Initialize the physical addresses that will be translated
    //

    physicalAddressEnd = RtlLargeIntegerAdd (physicalAddress,
                                             RtlConvertUlongToLargeInteger(
                                             length));

    //
    // Translate the physical addresses.
    //

    translateBaseAddress =
        HalTranslateBusAddress (interfaceType,
                                busNumber,
                                physicalAddress,
                                &inIoSpace,
                                &physicalAddressBase);

    translateEndAddress =
        HalTranslateBusAddress (interfaceType,
                                busNumber,
                                physicalAddressEnd,
                                &inIoSpace2,
                                &physicalAddressEnd);

    DbgPrint("inIoSpace: %d\n", inIoSpace);
    DbgPrint("inIoSpace2: %d\n", inIoSpace2);

    if ( !(translateBaseAddress && translateEndAddress) )
    {
        MapMemKdPrint (("MAPMEM.SYS: HalTranslatephysicalAddress failed\n"));

        ntStatus = STATUS_UNSUCCESSFUL;

        goto close_handle;
    }

    //
    // Calculate the length of the memory to be mapped
    //

    mappedLength = RtlLargeIntegerSubtract (physicalAddressEnd,
                                            physicalAddressBase);


    //
    // If the mappedlength is zero, somthing very weird happened in the HAL
    // since the Length was checked against zero.
    //

    if (mappedLength.LowPart == 0)
    {
        MapMemKdPrint (("MAPMEM.SYS: mappedLength.LowPart == 0\n"));

        ntStatus = STATUS_UNSUCCESSFUL;

        goto close_handle;
    }

    length = mappedLength.LowPart;


    //
    // If the address is in io space, just return the address, otherwise
    // go through the mapping mechanism
    //

    // if (inIoSpace)
    // {
    //     *((PVOID *) IoBuffer) = (PVOID) physicalAddressBase.LowPart;
    // }

    // else
    {
        //
        // initialize view base that will receive the physical mapped
        // address after the MapViewOfSection call.
        //

        viewBase = physicalAddressBase;


        //
        // Let ZwMapViewOfSection pick an address
        //

        virtualAddress = NULL;



        //
        // Map the section
        //

        ntStatus = ZwMapViewOfSection (physicalMemoryHandle,
                                       (HANDLE) -1,
                                       &virtualAddress,
                                       0L,
                                       length,
                                       &viewBase,
                                       &length,
                                       ViewShare,
                                       0,
                                       PAGE_READWRITE | PAGE_NOCACHE);

        if (!NT_SUCCESS(ntStatus))
        {
            MapMemKdPrint (("MAPMEM.SYS: ZwMapViewOfSection failed\n"));

            goto close_handle;
        }

        //
        // Mapping the section above rounded the physical address down to the
        // nearest 64 K boundary. Now return a virtual address that sits where
        // we wnat by adding in the offset from the beginning of the section.
        //


        (ULONG) virtualAddress += (ULONG)physicalAddressBase.LowPart -
                                  (ULONG)viewBase.LowPart;

        *((PVOID *) IoBuffer) = virtualAddress;

    }

    ntStatus = STATUS_SUCCESS;



close_handle:

    ZwClose (physicalMemoryHandle);



done:

    return ntStatus;
}
