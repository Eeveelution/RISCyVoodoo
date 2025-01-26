#include "CardDetection.h"

DetectionResult DetectVoodooCard() {
    DetectionResult returnStructure;

    PCI_SLOT_NUMBER SlotNumber;
	PPCI_COMMON_CONFIG PciData;
	UCHAR buffer[PCI_COMMON_HDR_LENGTH];
	ULONG i, f, j, bus, k;
	BOOLEAN flag = TRUE;
	
	PciData = (PPCI_COMMON_CONFIG) buffer;
	SlotNumber.u.bits.Reserved = 0;

    RtlZeroMemory(&returnStructure, sizeof(DetectionResult));

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

                switch(PciData->DeviceID) {
                    case 1:
                        returnStructure.cardType = CARD_TYPE_VOODOO1;
                        break;
                    case 2:
                        returnStructure.cardType = CARD_TYPE_VOODOO2;
                        break;
                    case 3:
                    case 4:
                        returnStructure.cardType = CARD_TYPE_VOODOO_BANSHEE;
                        break;
                    case 5:
                    case 57:
                        returnStructure.cardType = CARD_TYPE_VOODOO3;
                        break;
                    case 9:
                        returnStructure.cardType = CARD_TYPE_VOODOO45;
                        break;
                }

                DbgPrint("Found card: %s\n", CardTypeToString(returnStructure.cardType));

                returnStructure.busNumber = bus;
                returnStructure.deviceNumber = i;
                returnStructure.deviceId = PciData->DeviceID;
                
                return returnStructure;
            }
		}
	}

    end:;
}