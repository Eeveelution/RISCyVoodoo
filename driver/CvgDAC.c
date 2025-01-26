#include "CvgDAC.h"

void DACWrite(SstRegs* sst, ULONG addr, ULONG data) {
    ISET(sst->dacData,
        (data & SST_DACDATA_DATA) |
        ((addr) << SST_DACDATA_ADDR_SHIFT) |
        SST_DACDATA_WR);

    DbgPrint("DAC: Writing 0x%x to 0x%x\n", addr, data);

    sst1InitIdleFBINoNOP(sst);


}

ULONG DACRead(SstRegs* sst, ULONG addr) {
    ULONG returnData;
    ISET(sst->dacData, ((addr) << SST_DACDATA_ADDR_SHIFT) | SST_DACDATA_RD);

    DbgPrint("DAC: Dac data address set;", addr);

    sst1InitIdleFBINoNOP(sst);

    returnData = IGET(sst->fbiInit2) & SST_DACDATA_DATA;

    DbgPrint("Read: 0x%x\n", returnData);

    return returnData;
}

BOOLEAN detectDacATTorTI(SstRegs* sst, ULONG mirDefault, ULONG dirDefault) {
    int i;

    DbgPrint("DACDETECT: Attempting to detect ATT or TI DAC\n");

    //Try to detect ATT/TI DAC
    //apperantly sometimes DACs don't feel like initializing so just init it like 100 times
    for(i = 0; i != 100; i++) {
        ULONG j, dacmir, dacdir;

        //guarantee no rendering is happening
        sst1InitIdleFBINoNOP(sst);

        DACWrite(sst, SST_DACREG_WMA, 0x0);

        //glide driver does this 5 times
        DACRead(sst, SST_DACREG_RMR);
        DACRead(sst, SST_DACREG_RMR);
        DACRead(sst, SST_DACREG_RMR);
        DACRead(sst, SST_DACREG_RMR);
        DACRead(sst, SST_DACREG_RMR);

        //enable indexed programming
        DACWrite(sst, SST_DACREG_WMA, 0x0);
        
        DACRead(sst, SST_DACREG_RMR);
        DACRead(sst, SST_DACREG_RMR);
        DACRead(sst, SST_DACREG_RMR);
        DACRead(sst, SST_DACREG_RMR);

        //finally write the indexed programming config
        DACWrite(sst, SST_DACREG_RMR, SST_DACREG_CR0_INDEXED_ADDRESSING | SST_DACREG_CR0_8BITDAC);

        j = 0;

        DACWrite(sst, SST_DACREG_WMA, SST_DACREG_INDEX_MIR);

        dacmir = DACRead(sst, SST_DACREG_INDEXDATA);

        if(dacmir == mirDefault) {
            j++;
        } else continue;

        DACWrite(sst, SST_DACREG_INDEXADDR, SST_DACREG_INDEX_DIR);

        dacdir = DACRead(sst, SST_DACREG_INDEXDATA);

        if(dacmir == mirDefault && dacdir == dirDefault) {
            j++;
        } else continue;

        //found DAC!
        if(j == 2) {
            DbgPrint("Detected DAC! Attempts: %d\n", i);

            sst1InitIdleFBINoNOP(sst);

            DACWrite(sst, SST_DACREG_INDEXADDR, SST_DACREG_INDEX_CR0);
            DACWrite(sst, SST_DACREG_INDEXDATA, DACRead(sst, SST_DACREG_INDEXDATA) & ~SST_DACREG_CR0_INDEXED_ADDRESSING);

            return TRUE;
        }
    }

    return FALSE;
}

BOOLEAN detectDacICS(SstRegs* sst) {
    int i;

    DbgPrint("DACDETECT: Attempting to detect ICS DAC\n");

    for(i = 0; i != 100; i++) {
        FxU32 gclk1, vclk1, vclk7;

        sst1InitIdleFBINoNOP(sst);

        DACWrite(sst, SST_DACREG_ICS_PLLADDR_RD, SST_DACREG_ICS_PLLADDR_GCLK1);
        
        gclk1 = DACRead(sst,  SST_DACREG_ICS_PLLADDR_DATA);
        
        DACRead(sst,  SST_DACREG_ICS_PLLADDR_DATA);
        DACWrite(sst, SST_DACREG_ICS_PLLADDR_RD, SST_DACREG_ICS_PLLADDR_VCLK1);
        vclk1 = DACRead(sst, SST_DACREG_ICS_PLLADDR_DATA);
        
        DACRead(sst, SST_DACREG_ICS_PLLADDR_DATA);
        DACWrite(sst, SST_DACREG_ICS_PLLADDR_RD, SST_DACREG_ICS_PLLADDR_VCLK7);
        
        vclk7 = DACRead(sst,  SST_DACREG_ICS_PLLADDR_DATA);
        
        DACRead(sst,  SST_DACREG_ICS_PLLADDR_DATA);
        
        if(gclk1 == SST_DACREG_ICS_PLLADDR_GCLK1_DEFAULT &&
           vclk1 == SST_DACREG_ICS_PLLADDR_VCLK1_DEFAULT &&
           vclk7 == SST_DACREG_ICS_PLLADDR_VCLK7_DEFAULT
        ) {
            DbgPrint("Detected DAC! Attempts: %d\n", i);

            //found DAC!
            return TRUE;
        }
    }

    return FALSE;
}

DacType CvgDetectDac(DetectionResult voodoo, SstRegs* sst) {
    ULONG i;
    PCI_SLOT_NUMBER slotNumber;

    ULONG dacReadEnableDisableFifoWrite;
    ULONG writeEnableFifoWriteEnable;

    ULONG oldFbiInit1, oldFbiInit2;

    DacType type;

    slotNumber.u.bits.DeviceNumber = voodoo.deviceNumber;
    slotNumber.u.bits.FunctionNumber = 0;
    slotNumber.u.bits.Reserved = 0;

    sst1InitIdleFBINoNOP(sst);

    oldFbiInit1 = IGET(sst->fbiInit1);
    oldFbiInit2 = IGET(sst->fbiInit2);

    /* Reset video unit to guarantee no contentions on the memory bus */
    ISET(sst->fbiInit1, IGET(sst->fbiInit1) | SST_VIDEO_RESET);
    /* Turn off dram refresh to guarantee no contentions on the memory bus */
    ISET(sst->fbiInit2, IGET(sst->fbiInit2) & ~SST_EN_DRAM_REFRESH);

    sst1InitIdleFBINoNOP(sst);

    dacReadEnableDisableFifoWrite = SST_INITWR_EN | SST_FBIINIT23_REMAP;

    i = HalSetBusDataByOffset(PCIConfiguration, voodoo.busNumber, slotNumber.u.AsULONG, &dacReadEnableDisableFifoWrite, 0x40, 4); DbgPrint("Dac Read Enable: %d\n", i);

    sst1InitIdleFBINoNOP(sst);

    if(detectDacATTorTI(sst, SST_DACREG_INDEX_MIR_ATT_DEFAULT, SST_DACREG_INDEX_DIR_ATT_DEFAULT)) {
        DbgPrint("Detected ATT DAC.\n");

        type = CVG_DACTYPE_ATT;
    } else if (detectDacATTorTI(sst, SST_DACREG_INDEX_MIR_TI_DEFAULT, SST_DACREG_INDEX_DIR_TI_DEFAULT)) {
        DbgPrint("Detected TI DAC.\n");

        type = CVG_DACTYPE_TI;
    } else if(detectDacICS(sst)) {
        DbgPrint("Detected ICS DAC.\n");

        type = CVG_DACTYPE_ICS;
    } else {
        DbgPrint("No DAC detected!\n");

        type = CVG_DACTYPE_NONE;
    }

    writeEnableFifoWriteEnable = SST_INITWR_EN | SST_PCI_FIFOWR_EN;

    i = HalSetBusDataByOffset(PCIConfiguration, voodoo.busNumber, slotNumber.u.AsULONG, &writeEnableFifoWriteEnable, 0x40, 4); DbgPrint("fifo write enable: %d\n", i);

    /* Restore init register states */
    sst1InitIdleFBINoNOP(sst);

    ISET(sst->fbiInit1, oldFbiInit1);
    ISET(sst->fbiInit2, oldFbiInit2);

    sst1InitIdleFBINoNOP(sst);

    return type;
}