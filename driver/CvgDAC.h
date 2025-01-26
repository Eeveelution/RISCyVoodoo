#ifndef __CVG_DAC_H_
#define __CVG_DAC_H_

#include "../3dfx/all.h"
#include "../CardDetection.h"

#define CVG_DACTYPE_NONE 0x0
#define CVG_DACTYPE_ATT  0x1
#define CVG_DACTYPE_TI   0x2
#define CVG_DACTYPE_ICS  0x3

typedef ULONG DacType;

DacType CvgDetectDac(DetectionResult voodoo, SstRegs* sst);

#endif