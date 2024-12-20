#include "MacTypes.r" // for vers type
#include "IntlResources.r" // for itlc type

// boot sector
data 'boot' (1, sysHeap, protected) {
	$$read("apple/boot1.bin")
};

// mixedmode routine descriptor followed by our PEF
data 'boot' (2) {
	$$read("stage1.bin")
};

// version info, needed for Startup Disk to allow this to be selected
resource 'vers' (1, purgeable) {
	0x00, // Major version
	0x00, // Minor version
	release, // Release stage
	0x00, // Non-final release number
	verUS, // Region code
	"0.0", // short version string
	"0.0" // long version string
};

// this is needed or InitResources (called by boot sector) dies
resource 'itlc' (0, sysHeap, purgeable) {
	0, 0x800,
	noFontForce, intlForce, noOldKeyboard,
	0, 40, rightOffset, 0, verUS, directionLeftRight,
	$""
};