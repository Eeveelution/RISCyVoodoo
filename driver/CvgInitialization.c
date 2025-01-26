#include "CvgInitialization.h"

#include "CvgDAC.h"

void PrintVoodooStatus(ULONG status) {
    DbgPrint("VRETRACE: %d | ", status & SST_VRETRACE);
    DbgPrint("FBI_BUSY: %d | ", status & SST_FBI_BUSY);
    DbgPrint("TMU_BUSY: %d | ", status & SST_TMU_BUSY);
    DbgPrint("SST_BUSY: %d | ", status & SST_TREX_BUSY);
    DbgPrint("FIFO_LVL: %d\n",  status & SST_FIFOLEVEL);
}

#define STALL_STATUS() PrintVoodooStatus(IGET(sst->status)); PrintVoodooStatus(IGET(sst->status)); PrintVoodooStatus(IGET(sst->status))

void InitializeCvg(DetectionResult voodoo, SstRegs* sst) {
    ULONG i;
    PCI_SLOT_NUMBER slotNumber;

    ULONG treshold = 0x8;
    ULONG clockDelay = 0x8;

    ULONG initWriteEnable = BIT(0);
    ULONG snoopDefault = 0;

    ULONG writeAndFifoWriteEnable = SST_INITWR_EN | SST_PCI_FIFOWR_EN;

    DacType dacDetected;

    slotNumber.u.bits.DeviceNumber = voodoo.deviceNumber;
    slotNumber.u.bits.FunctionNumber = 0;
    slotNumber.u.bits.Reserved = 0;

    if(IGET(sst->fbiInit1) & SST_EN_SCANLINE_INTERLEAVE) {
        DbgPrint("SLI active!\n");
    } else {
        DbgPrint("SLI not active!\n");
    }
    
    DbgPrint("Enabling writes to FBIINIT and reseting bus snooping\n");

    i = HalSetBusDataByOffset(PCIConfiguration, voodoo.busNumber, slotNumber.u.AsULONG, &initWriteEnable, 0x40, 4); DbgPrint("Init Write: %d\n", i);
    i = HalSetBusDataByOffset(PCIConfiguration, voodoo.busNumber, slotNumber.u.AsULONG, &snoopDefault, 0x44, 4); DbgPrint("Snoop0: %d\n", i);
    i = HalSetBusDataByOffset(PCIConfiguration, voodoo.busNumber, slotNumber.u.AsULONG, &snoopDefault, 0x48, 4); DbgPrint("Snoop1: %d\n", i);

    STALL_STATUS();

    DbgPrint("fbiInit3 happening\n");

    //apperantly adjusts the FBI and TREX fifo
    ISET(sst->fbiInit3,
        (SST_FBIINIT3_DEFAULT & ~(SST_FT_CLK_DEL_ADJ | SST_TF_FIFO_THRESH)) |
        (clockDelay << SST_FT_CLK_DEL_ADJ_SHIFT) |
        (treshold << SST_TF_FIFO_THRESH_SHIFT)
    );

    STALL_STATUS();

    DbgPrint("Resetting graphics and video units\n");

    ISET(sst->fbiInit1, IGET(sst->fbiInit1) | SST_VIDEO_RESET);

    STALL_STATUS();

    DbgPrint("fbiInit0\n");

    ISET(sst->fbiInit0, IGET(sst->fbiInit0) | (SST_GRX_RESET | SST_PCI_FIFO_RESET));

    DbgPrint("waiting...\n");

    sst1InitIdleFBINoNOP(sst);

    DbgPrint("unresetting fbiInit0\n");

    //unreset necessary as else the FIFO won't clear
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
    
    DbgPrint("waiting for regs to reset..\n");

    sst1InitIdleFBINoNOP(sst);  // Wait until init regs are reset

    HalSetBusDataByOffset(PCIConfiguration, voodoo.busNumber, slotNumber.u.AsULONG, &writeAndFifoWriteEnable, 0x40, 4);

    dacDetected = CvgDetectDac(voodoo, sst);
}