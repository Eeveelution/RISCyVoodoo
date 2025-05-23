#define _M_ALPHA
#define _ALPHA_

#include "ntddk.h"
#include "stdarg.h"

#include "DriverEntry.h"
#include "CardDetection.h"

#include "3dfx/all.h"

#include "CvgInitialization.h"

NTSTATUS MapMemDispatch(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
);

VOID MapMemUnload(
    IN PDRIVER_OBJECT DriverObject
);

NTSTATUS MapMemMapTheMemory(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PVOID      ioBuffer,
    IN ULONG          inputBufferLength,
    IN ULONG          outputBufferLength
);

static void* VOODOOPTR;


static PHYSICAL_ADDRESS voodooBaseAddr;


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
        PCI_SLOT_NUMBER slotNumber;
        PCI_COMMON_CONFIG pciConfig;
        ULONG i;
        PCM_RESOURCE_LIST allocatedResources;
        ULONG isInIoSpace = 4;
        PHYSICAL_ADDRESS voodooPhysAddr;
        PVOID mappedAddress;
        DetectionResult voodoo;
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

            DbgPrint (("MAPMEM.SYS: IoCreateSymbolicLink failed\n"));

            IoDeleteDevice (deviceObject);
        }

        voodoo = DetectVoodooCard();

        slotNumber.u.bits.DeviceNumber = voodoo.deviceNumber;
        slotNumber.u.bits.FunctionNumber = 0;
        slotNumber.u.bits.Reserved = 0;

        ntStatus = HalAssignSlotResources(RegistryPath, NULL, DriverObject, deviceObject, PCIBus, voodoo.busNumber, voodoo.deviceNumber, &allocatedResources);

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

        i = HalGetBusData(PCIConfiguration, voodoo.busNumber, slotNumber.u.AsULONG, &pciConfig, PCI_COMMON_HDR_LENGTH);

        DbgPrint("HalGetBusData: %d\n", i);

        pciConfig.LatencyTimer = 0xFF;
        pciConfig.Command = 0x0006; // bus master & memory space enable

        i = HalSetBusData(PCIConfiguration, voodoo.busNumber, slotNumber.u.AsULONG, &pciConfig, PCI_COMMON_HDR_LENGTH);

        DbgPrint("HalSetBusData: %d\n", i);

        ntStatus = HalTranslateBusAddress(PCIBus, voodoo.busNumber, allocatedResources->List[0].PartialResourceList.PartialDescriptors[0].u.Memory.Start, &isInIoSpace, &voodooPhysAddr);

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

            successBase = HalTranslateBusAddress(PCIBus, voodoo.busNumber, voodooPhysAddr, &isInIoSpace1, &baseAddr);
            successEnd = HalTranslateBusAddress(PCIBus, voodoo.busNumber, basePlusLength, &isInIoSpace2, &endAddr);

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

            {
                LARGE_INTEGER endMinusBase;
                endMinusBase = RtlLargeIntegerSubtract (endAddr, baseAddr);

                DbgPrint("Mapped length: %d\n", endMinusBase);
            }

            VOODOOPTR = mappedAddress;

            voodooBaseAddr = baseAddr;

            // return;

            InitializeCvg(voodoo, (SstRegs*) mappedAddress);   
        }
    }
    else
    {
        DbgPrint (("MAPMEM.SYS: IoCreateDevice failed\n"));
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

        DbgPrint (("MAPMEM.SYS: IRP_MJ_CREATE\n"));

        break;



    case IRP_MJ_CLOSE:

        DbgPrint (("MAPMEM.SYS: IRP_MJ_CLOSE\n"));

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

                DbgPrint (("MAPMEM.SYS: memory successfully mapped\n"));
            }

            else
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;

                DbgPrint (("MAPMEM.SYS: memory map failed :(\n"));
            }

            break;



        case IOCTL_MAPMEM_UNMAP_USER_PHYSICAL_MEMORY:

            if (inputBufferLength >= sizeof(PVOID))
            {
                Irp->IoStatus.Status = ZwUnmapViewOfSection ((HANDLE) -1,
                                                             *((PVOID *) ioBuffer)
                                                             );

                DbgPrint (("MAPMEM.SYS: memory successfully unmapped\n"));
            }
            else
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;

                DbgPrint (("MAPMEM.SYS: ZwUnmapViewOfSection failed\n"));
            }

            break;



        default:

            DbgPrint (("MAPMEM.SYS: unknown IRP_MJ_DEVICE_CONTROL\n"));

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

    DbgPrint (("MAPMEM.SYS: unloading\n"));

    IoDeleteDevice (DriverObject->DeviceObject);
}



NTSTATUS
MapMemMapTheMemory(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PVOID      IoBuffer,
    IN ULONG          InputBufferLength,
    IN ULONG          OutputBufferLength
    )
{
    // INTERFACE_TYPE     interfaceType;
    // ULONG              busNumber;
    // PHYSICAL_ADDRESS   physicalAddress;
    ULONG              length = 16 * 1024 * 1024;
    UNICODE_STRING     physicalMemoryUnicodeString;
    OBJECT_ATTRIBUTES  objectAttributes;
    HANDLE             physicalMemoryHandle  = NULL;
    PVOID              PhysicalMemorySection = NULL;
    // ULONG              inIoSpace, inIoSpace2;
    NTSTATUS           ntStatus;
    // PHYSICAL_ADDRESS   physicalAddressBase;
    // PHYSICAL_ADDRESS   physicalAddressEnd;
    PHYSICAL_ADDRESS   viewBase;
    // PHYSICAL_ADDRESS   mappedLength;
    // BOOLEAN            translateBaseAddress;
    // BOOLEAN            translateEndAddress;
    PVOID              virtualAddress;

    // UNICODE_STRING     physicalMemoryUnicodeString;

    RtlInitUnicodeString (&physicalMemoryUnicodeString,
        L"\\Device\\PhysicalMemory");

    DbgPrint("initializing attribs\n");

    InitializeObjectAttributes (&objectAttributes,
                            &physicalMemoryUnicodeString,
                            OBJ_CASE_INSENSITIVE,
                            (HANDLE) NULL,
                            (PSECURITY_DESCRIPTOR) NULL);

    DbgPrint("opening section\n");

    ntStatus = ZwOpenSection (&physicalMemoryHandle,
                            SECTION_ALL_ACCESS,
                            &objectAttributes);

    if (!NT_SUCCESS(ntStatus))
    {
        DbgPrint (("MAPMEM.SYS: ZwOpenSection failed\n"));

        return;
    }

    DbgPrint("ObReferenceObjectByHandle\n");

    ntStatus = ObReferenceObjectByHandle (physicalMemoryHandle,
                                        SECTION_ALL_ACCESS,
                                        (POBJECT_TYPE) NULL,
                                        KernelMode,
                                        &PhysicalMemorySection,
                                        (POBJECT_HANDLE_INFORMATION) NULL);

    if (!NT_SUCCESS(ntStatus)) {
        DbgPrint (("MAPMEM.SYS: ObReferenceObjectByHandle failed\n"));

        return;
    }

    DbgPrint("viewBase = 0x%x\n", voodooBaseAddr);

    viewBase = voodooBaseAddr;


    //
    // Let ZwMapViewOfSection pick an address
    //

    virtualAddress = NULL;



    //
    // Map the section
    //

    DbgPrint("MapViewOfSection\n");

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
            DbgPrint (("MAPMEM.SYS: ZwMapViewOfSection failed\n"));

            return;
        }

        //
        // Mapping the section above rounded the physical address down to the
        // nearest 64 K boundary. Now return a virtual address that sits where
        // we wnat by adding in the offset from the beginning of the section.
        //

        DbgPrint("Virtual Address pre-adding: 0x%x\n", virtualAddress);
        
        
        (ULONG) virtualAddress += (ULONG)voodooBaseAddr.LowPart - (ULONG)viewBase.LowPart;

        DbgPrint("Virtual Address post-adding: 0x%x\n", virtualAddress);

        *((PVOID *) IoBuffer) = virtualAddress;
}
