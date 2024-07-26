// MIO detection routines for drivers
// Scans the PCI bus to look for supported MIO cards;
// provides to caller its type and base address.
// Only supports GrandCentral, Heathrow/Paddington, and KeyLargo.

typedef enum _MIO_TYPE {
	MIO_UNKNOWN,
	MIO_GRANDCENTRAL,
	MIO_OHARE,
	MIO_PADDINGTON,
	MIO_KEYLARGO
} MIO_TYPE;

// KEYLARGO has:
// (MPIC at 0x40000)
// +0x10000 - i2s
// +0x11000 -
// +0x12000 - legacy serial
// +0x13000 - serial
// +0x14000 - audio
// +0x15000 - timer
// +0x16000 - pxi
// +0x17000 -
// +0x18000 - i2c
// +0x19000 -
// +0x1a000 -
// +0x1b000 -
// +0x1c000 -
// +0x1d000 -
// +0x1e000 -
// +0x1f000 - ultra ata (irq 19, dma 10)
// +0x20000 - primary ata (irq 20, dma 11)
// +0x21000 - secondary ata (irq 21, dma 12)
// +0x30000 - wifi

// PADDINGTON (and HEATHROW) has:
// (custom interrupt controller, two of them)
// +0x10000 - scsi (MESH)
// +0x11000 - ethernet
// +0x12000 - legacy serial
// +0x13000 - serial
// +0x14000 - audio
// +0x15000 - fdc
// +0x16000 - pxi
// +0x17000 -
// +0x18000 - i2c
// +0x19000 -
// +0x1a000 -
// +0x1b000 -
// +0x1c000 -
// +0x1d000 -
// +0x1e000 -
// +0x1f000 -
// +0x20000 - primary ata (irq 13, dma 11, dmairq 2)
// +0x21000 - secondary ata (irq 14, dma 12, dmairq 3)
// +0x60000 - nvram 8KB

// O'HARE has:
// (custom interrupt controller, one of them)
// +0x10000 - scsi (MESH)
// +0x11000 -
// +0x12000 - legacy serial
// +0x13000 - serial
// +0x14000 - audio
// +0x15000 - fdc
// +0x16000 - pxi
// +0x17000 -
// +0x18000 -
// +0x19000 -
// +0x1a000 -
// +0x1b000 -
// +0x1c000 -
// +0x1d000 -
// +0x1e000 -
// +0x1f000 -
// +0x20000 - primary ata (irq 13, dma 11, dmairq 2)
// +0x21000 - secondary ata (irq 14, dma 12, dmairq 3)
// +0x60000 - nvram 8KB

// GRANDCENTRAL has:
// (custom interrupt controller, one of them)
// +0x10000 - external scsi, 35CF94 compatible
// +0x11000 - ethernet, Am79C940 compatible
// +0x12000 - legacy serial
// +0x13000 - serial
// +0x14000 - audio
// +0x15000 - fdc
// +0x16000 - pxi
// +0x17000 - 
// +0x18000 - internal scsi, 35C94 derivative (MESH)
// +0x19000 - ethernet ROM / mac address, mirrored across the entire space
// +0x1a000 - board registers
// +0x1b000 - dac
// +0x1c000 - video
// +0x1d000 - nvram index
// +0x1e000 - video (other register)
// +0x1f000 - nvram data

#ifdef MIO_DRV_DETECT_IMPL

#include "pci.h"

static MIO_TYPE MioDoDetect(PPHYSICAL_ADDRESS BaseAddress) {
	PCI_SLOT_NUMBER SlotNumber;
	PPCI_COMMON_CONFIG PciData;
	UCHAR buffer[PCI_COMMON_HDR_LENGTH];
	ULONG i, f, j, bus;
	BOOLEAN flag = TRUE;
	MIO_TYPE RetVal = MIO_UNKNOWN;
	
	PciData = (PPCI_COMMON_CONFIG) buffer;
	SlotNumber.u.bits.Reserved = 0;
	if (BaseAddress != NULL) BaseAddress->QuadPart = 0;
	
	for (bus = 0; flag; bus++) {
		for (i = 0; i < PCI_MAX_DEVICES && flag; i++) {
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
			
			if (j == 0) {
				// no more buses
				flag = FALSE;
				break;
			}
			
			// vendor id: apple
			if (PciData->VendorID != 0x106b) continue;
			
			USHORT DeviceID = PciData->DeviceID;
			if (
				DeviceID != 0x0002 && // grandcentral
				DeviceID != 0x0007 && // o'hare
				DeviceID != 0x0010 && // heathrow
				DeviceID != 0x0017 && // paddington
				DeviceID != 0x0022 && // keylargo
				DeviceID != 0x0025 && // keylargo (pangaea)
				// BUGBUG: is intrepid really supported?
				// i think the MIO part might be same based on some info?
				// the only thing that's different is USB and maybe some PXI commands (not anything HAL uses)?
				DeviceID != 0x003e    // keylargo (intrepid)
				// K2 (0x0041) and Shasta (0x004F) are PMG5, unsupported
			) continue;
			
			// this is a supported MIO controller.
			if (DeviceID == 0x0002) RetVal = MIO_GRANDCENTRAL;
			else if (DeviceID == 0x0x0007) RetVal = MIO_OHARE;
			else RetVal = (DeviceID < 0x0020) ? MIO_PADDINGTON : MIO_KEYLARGO;
			if (BaseAddress != NULL) BaseAddress->LowPart = PciData->u.type0.BaseAddresses[0];
			return RetVal;
		}
	}
	
	return MIO_UNKNOWN;
}

#endif