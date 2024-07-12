#include <stddef.h>
#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include "processor.h"
#include "arc.h"
#include "arcterm.h"
#include "arcenv.h"
#include "pxi.h"
#include "runtime.h"

static void ArcHalt(void) {
	PxiPowerOffSystem(true);
}

static void ArcPowerOff(void) {
	PxiPowerOffSystem(false);
}

static void ArcRestart(void) {
	PxiPowerOffSystem(true);
}

static void ArcReboot(void) {
	PxiPowerOffSystem(true);
}

static void ArcEnterInteractiveMode(void) {
	PxiPowerOffSystem(true);
}

void ArcTermInit(void) {
	// Initialise the functions implemented here.
	PVENDOR_VECTOR_TABLE Api = ARC_VENDOR_VECTORS();
	Api->HaltRoutine = ArcHalt;
	Api->PowerDownRoutine = ArcPowerOff;
	Api->RestartRoutine = ArcRestart;
	Api->RebootRoutine = ArcReboot;
	Api->InteractiveModeRoutine = ArcEnterInteractiveMode;
}