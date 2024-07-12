#pragma once
#include "types.h"

UCHAR PxiSendSyncRequest(UCHAR Command, PUCHAR Arguments, UCHAR ArgLength, BOOLEAN VariadicIn, PUCHAR Response, UCHAR ResponseLength, BOOLEAN VariadicOut);

void PxiInit(PVOID MmioBase, bool IsCuda);

ULONG PxiRtcRead(void);
void PxiRtcWrite(ULONG Value);
void PxiPowerOffSystem(bool Reset);
ULONG PxiAdbCommand(PUCHAR Command, ULONG Length, PUCHAR Response);
