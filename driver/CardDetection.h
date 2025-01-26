#ifndef __CARD_DETECTION_H_
#define __CARD_DETECTION_H_

#include "ntddk.h"

#define CARD_TYPE_NONE            0x0
#define CARD_TYPE_VOODOO1         0x1
#define CARD_TYPE_VOODOO_RUSH     0x2
#define CARD_TYPE_VOODOO2         0x3
#define CARD_TYPE_VOODOO_BANSHEE  0x4
#define CARD_TYPE_VOODOO3         0x5
#define CARD_TYPE_VOODOO45        0x6

typedef unsigned char CardType;

const char* CardTypeToString(CardType cardType) {
    switch(cardType) {
        case CARD_TYPE_NONE:
            return "None";
        case CARD_TYPE_VOODOO1:
            return "Voodoo 1";
        case CARD_TYPE_VOODOO_RUSH:
            return "Voodoo Rush";
        case CARD_TYPE_VOODOO2:
            return "Voodoo 2";
        case CARD_TYPE_VOODOO_BANSHEE:
            return "Voodoo Banshee";
        case CARD_TYPE_VOODOO3:
            return "Voodoo 3";
        case CARD_TYPE_VOODOO45:
            return "Voodoo 4/5";
        default:
            return "Unknown";
    }
}

typedef struct {
    CardType cardType;

    ULONG busNumber;
    ULONG deviceNumber;
    ULONG deviceId;
} DetectionResult;

DetectionResult DetectVoodooCard();

#endif