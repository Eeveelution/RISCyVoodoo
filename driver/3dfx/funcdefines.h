#ifndef __FUNCDEFINES_H_
#define __FUNCDEFINES_H_

#include "3dfx.h"
#include "cvgdefs.h"
#include "cvgregs.h"

FxU32 __stdcall sst1InitRead32(FxU32 *addr) {
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


#endif