// Open Firmware API

#include <stdarg.h>
#include <memory.h>
#include "ofapi.h"

static tfpOpenFirmwareCall s_OfCall;
static OFHANDLE s_OfChosen;

/// <summary>
/// Initialises the OF interface
/// </summary>
/// <param name="OfCall">Function pointer to the OF client interface.</param>
/// <returns>True on success, false on error</returns>
bool OfInit(tfpOpenFirmwareCall OfCall) {
	s_OfCall = OfCall;

	s_OfChosen = OfFindDevice("/chosen");
	if (s_OfChosen == OFNULL) return false;
	return true;
}

/// <summary>
/// Gets the component handle for /chosen - guaranteed to be present
/// </summary>
/// <returns>Component handle</returns>
OFHANDLE OfGetChosen(void) {
	return s_OfChosen;
}

static bool OfCall(POF_CALL_HEADER Args) {
	if (s_OfCall == NULL) return false;
	return s_OfCall(Args) == 0;
}

#define OF_CALL(args) OfCall(&((args).Header))

static bool OfpVoidCall(const char* func) {
	OF_CALL_HEADER Header = { func, 0, 0 };
	return OfCall(&Header);
}

static OFHANDLE OfpDtCall(const char* func, OFHANDLE Handle) {
	struct {
		OF_CALL_HEADER Header;
		OFHANDLE Handle;
		OFHANDLE OutHandle;
	} args = {
		{ func, 1, 1 },
		Handle,
		0
	};

	if (!OF_CALL(args)) return OFNULL;
	return args.OutHandle;
}

/// <summary>
/// Gets the peer of the current component, OF equivalent to ARC GetPeer
/// </summary>
/// <param name="Handle">Component to obtain the peer of</param>
/// <returns>Peer component</returns>
OFHANDLE OfPeer(OFHANDLE Handle) {
	// Imitate ARC API here, ARC returns NULL for Peer(NULL), OF returns root for Peer(NULL)
	if (Handle == OFNULL) return OFNULL;
	return OfpDtCall("peer", Handle);
}

/// <summary>
/// Gets the child of the current component, OF equivalent to ARC GetChild
/// </summary>
/// <param name="Component">Component to obtain the child of. If null, obtains the root component.</param>
/// <returns>Component</returns>
OFHANDLE OfChild(OFHANDLE Handle) {
	// Imitate ARC API here, OF uses Peer(NULL) for getting root, ARC uses Child(NULL)
	if (Handle == OFNULL) return OfpDtCall("peer", OFNULL);
	return OfpDtCall("child", Handle);
}

/// <summary>
/// Gets the parent of the current component, OF equivalent to ARC GetParent
/// </summary>
/// <param name="Component">Component to obtain the parent of.</param>
/// <returns>Parent component</returns>
OFHANDLE OfParent(OFHANDLE Handle) {
	return OfpDtCall("parent", Handle);
}

/// <summary>
/// Gets OF path name from component handle.
/// </summary>
/// <param name="Handle">Component to obtain the path name of</param>
/// <param name="Buffer">Buffer where the path name is written</param>
/// <param name="Length">Reference to length of buffer, entire length (without null terminator) written here</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfPackageToPath(OFHANDLE Handle, PCHAR Buffer, PULONG Length) {
	if (Length == NULL || Handle == OFNULL) return _EINVAL;

	struct {
		OF_CALL_HEADER Header;
		OFHANDLE Handle;
		PCHAR Buffer;
		ULONG Length;
		ULONG ReturnLength;
	} args = {
		{ "package-to-path", 3, 1 },
		Handle,
		Buffer,
		*Length,
		0
	};

	if (!OF_CALL(args)) return _EFAULT;
	if (args.ReturnLength == 0xFFFFFFFFul) return _EINVAL;
	*Length = args.ReturnLength;
	return _ESUCCESS;
}

/// <summary>
/// Gets OF path name from device handle.
/// </summary>
/// <param name="Handle">Device to obtain the path name of</param>
/// <param name="Buffer">Buffer where the path name is written</param>
/// <param name="Length">Reference to length of buffer, entire length (without null terminator) written here</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfInstanceToPath(OFIHANDLE Handle, PCHAR Buffer, PULONG Length) {
	if (Length == NULL || Handle == OFNULL) return _EINVAL;

	struct {
		OF_CALL_HEADER Header;
		OFIHANDLE Handle;
		PCHAR Buffer;
		ULONG Length;
		ULONG ReturnLength;
	} args = {
		{ "instance-to-path", 3, 1 },
		Handle,
		Buffer,
		*Length,
		0
	};

	if (!OF_CALL(args)) return _EFAULT;
	if (args.ReturnLength == 0xFFFFFFFFul) return _EINVAL;
	*Length = args.ReturnLength;
	return _ESUCCESS;
}

/// <summary>
/// Gets OF component handle from device handle.
/// </summary>
/// <param name="Handle">Device to obtain component handle of</param>
/// <returns>Component handle or OFNULL on error</returns>
OFHANDLE OfInstanceToPackage(OFIHANDLE Handle) {
	struct {
		OF_CALL_HEADER Header;
		OFIHANDLE Handle;
		OFHANDLE Return;
	} args = {
		{ "instance-to-package", 1, 1 },
		Handle,
		0
	};

	if (!OF_CALL(args)) return OFNULL;
	if (args.Return == 0xFFFFFFFFul) return OFNULL;
	return args.Return;
}

static ARC_STATUS OfpGetPropLen(OFHANDLE Handle, const char* Name, PULONG BufLen) {
	struct {
		OF_CALL_HEADER Header;
		OFHANDLE Handle;
		const char* Name;
		ULONG OutLen;
	} args = {
		{ "getproplen", 2, 1 },
		Handle,
		Name,
		0
	};

	if (!OF_CALL(args)) return _EFAULT;
	if (args.OutLen == 0xFFFFFFFFul) return _ENOENT;
	*BufLen = args.OutLen;
	return _ESUCCESS;
}

static ARC_STATUS OfpGetProp(OFHANDLE Handle, const char* Name, void* Buffer, PULONG BufLen) {
	struct {
		OF_CALL_HEADER Header;
		OFHANDLE Handle;
		const char* Name;
		void* Buffer;
		ULONG InLen;
		ULONG OutLen;
	} args = {
		{ "getprop", 4, 1 },
		Handle,
		Name,
		Buffer,
		*BufLen,
		0
	};

	if (!OF_CALL(args)) return _EFAULT;
	if (args.OutLen == 0xFFFFFFFFul) return _ENOENT;
	*BufLen = args.OutLen;
	return _ESUCCESS;
}

/// <summary>
/// Gets a property value of an Open Firmware device.
/// </summary>
/// <param name="Handle">OF device handle</param>
/// <param name="Name">Property name</param>
/// <param name="Buffer">Buffer to write property value into (if NULL, just gets length)</param>
/// <param name="BufLen">Reference to length, returns actual length of property value (if 0, just gets length)</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfGetProperty(OFHANDLE Handle, const char* Name, void* Buffer, PULONG BufLen) {
	// BufLen must not be NULL
	if (BufLen == NULL) return _EINVAL;
	// if zero length or null buffer was passed, then get the length
	if (Buffer == NULL || *BufLen == 0) return OfpGetPropLen(Handle, Name, BufLen);
	// get the property
	return OfpGetProp(Handle, Name, Buffer, BufLen);
}

/// <summary>
/// Gets the next property name of an Open Firmware device
/// </summary>
/// <param name="Handle">OF device handle</param>
/// <param name="CurrentName">Current property name</param>
/// <param name="NextName">Buffer to write next property name to</param>
/// <returns>ARC status code: EINVAL for invalid arguments, EFAULT if call failed, ENOENT if no more properties</returns>
ARC_STATUS OfGetNextProperty(OFHANDLE Handle, const char* CurrentName, POF_NEXT_NAME NextName) {
	if (CurrentName == NULL || NextName == NULL) return _EINVAL;

	struct {
		OF_CALL_HEADER Header;
		OFHANDLE Handle;
		const char* CurrentName;
		POF_NEXT_NAME NextName;
		ULONG Result;
	} args = {
		{ "nextprop", 3, 1 },
		Handle,
		CurrentName,
		NextName,
		0
	};

	if (!OF_CALL(args)) return _EFAULT;

	if (args.Result == 0xFFFFFFFFul) return _EINVAL;
	if (args.Result == 0) return _ENOENT;
	return _ESUCCESS;
}

/// <summary>
/// Sets a property value of an Open Firmware device
/// </summary>
/// <param name="Handle">OF device handle</param>
/// <param name="Name">Name of property to set</param>
/// <param name="Buffer">Buffer containing value to set</param>
/// <param name="BufLen">Reference to length of value, on success set to actual length</param>
/// <returns>ARC status code.</returns>
ARC_STATUS OfSetProperty(OFHANDLE Handle, const char* Name, PVOID Buffer, PULONG BufLen) {
	if (Name == NULL || Buffer == NULL || BufLen == NULL) return _EINVAL;

	struct {
		OF_CALL_HEADER Header;
		OFHANDLE Handle;
		const char* Name;
		void* Buffer;
		ULONG InLen;
		ULONG OutLen;
	} args = {
		{ "setprop", 4, 1 },
		Handle,
		Name,
		Buffer,
		*BufLen,
		0
	};

	if (!OF_CALL(args)) return _EFAULT;
	if (args.OutLen == 0xFFFFFFFFul) return _EFAULT;
	*BufLen = args.OutLen;
	return _ESUCCESS;
}

/// <summary>
/// Gets a device handle from an Open Firmware device path.
/// </summary>
/// <param name="Name">Device path</param>
/// <returns>Device handle, or OFNULL on error.</returns>
OFHANDLE OfFindDevice(const char* Name) {
	if (Name == NULL) return OFNULL;

	struct {
		OF_CALL_HEADER Header;
		const char* Name;
		OFHANDLE Handle;
	} args = {
		{ "finddevice", 1, 1 },
		Name,
		0
	};

	if (!OF_CALL(args)) return OFNULL;
	if (args.Handle == 0xFFFFFFFFul) return OFNULL;
	return args.Handle;
}

/// <summary>
/// Determines if a device property exists.
/// </summary>
/// <param name="Handle">Device handle</param>
/// <param name="Name">Property name</param>
/// <returns>True if exists, false if not</returns>
bool OfPropExists(OFHANDLE Handle, const char* Name) {
	ULONG BufLen;
	ARC_STATUS Status = OfpGetPropLen(Handle, Name, &BufLen);
	if (ARC_FAIL(Status)) return false;
	return true;
}

/// <summary>
/// Gets an integer property.
/// </summary>
/// <param name="Handle">Device handle</param>
/// <param name="Name">Property name</param>
/// <param name="Value">Value written here on success</param>
/// <returns>ARC status code.</returns>
ARC_STATUS OfGetPropInt(OFHANDLE Handle, const char* Name, ULONG* Value) {
	if (Value == NULL) return _EINVAL;
	ULONG Length = sizeof(*Value);
	BYTE LocalVal[sizeof(*Value)];
	ARC_STATUS Status = OfpGetProp(Handle, Name, LocalVal, &Length);
	if (ARC_FAIL(Status)) return Status;
	if (Length != sizeof(*Value)) return _E2BIG;
	memcpy(Value, LocalVal, sizeof(LocalVal));
	return _ESUCCESS;
}

/// <summary>
/// Gets a register property.
/// </summary>
/// <param name="Handle">Device handle</param>
/// <param name="Name">Property name</param>
/// <param name="Index">Register index</param>
/// <param name="Register">Value written here on success</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfGetPropReg(OFHANDLE Handle, const char* Name, ULONG Index, POF_REGISTER Register) {
	if (Register == NULL) return _EINVAL;
	ULONG Length;
	ARC_STATUS Status = OfpGetPropLen(Handle, Name, &Length);
	if (ARC_FAIL(Status)) return Status;

	static BYTE s_PropReg[0x1000];
	if (Length >= 0x1000) {
		// sorry what
		return _E2BIG;
	}

	ULONG ReadLength = Length;
	Status = OfpGetProp(Handle, Name, s_PropReg, &ReadLength);
	if (ReadLength != Length) return _E2BIG;

	OFHANDLE Parent = OfParent(Handle);
	ULONG AddressCells = 0, SizeCells = 0;
	Status = OfGetPropInt(Parent, "#address-cells", &AddressCells);
	if (ARC_FAIL(Status)) AddressCells = 2;
	Status = OfGetPropInt(Parent, "#size-cells", &SizeCells);
	if (ARC_FAIL(Status) || SizeCells == 0) SizeCells = 1;

	ULONG Offset = Index * (AddressCells + SizeCells) * 4;
	PULONG pReg = (PULONG)&s_PropReg[Offset];
	if (Offset >= Length) return _E2BIG;
	//ULONG RemainingLength = Length - Offset;

	// Copy the physical address base.
	if (AddressCells == 1) {
		memcpy(&Register->PhysicalAddress.HighPart, &pReg[0], sizeof(*pReg));
		Register->PhysicalAddress.LowPart = Register->PhysicalAddress.HighPart;
	}
	else {
		memcpy(&Register->PhysicalAddress.HighPart, &pReg[AddressCells - 2], sizeof(*pReg));
		memcpy(&Register->PhysicalAddress.LowPart, &pReg[AddressCells - 1], sizeof(*pReg));
	}
	// And copy the size.
	memcpy(&Register->Size, &pReg[AddressCells + SizeCells - 1], sizeof(*pReg));

	return _ESUCCESS;
}

/// <summary>
/// Gets all registers of a register property.
/// </summary>
/// <param name="Handle">Device handle</param>
/// <param name="Name">Property name</param>
/// <param name="Register">Buffer where values are written, can be NULL</param>
/// <param name="Count">Number of registers</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfGetPropRegs(OFHANDLE Handle, const char* Name, POF_REGISTER Register, PULONG Count) {
	if (Count == NULL) return _EINVAL;
	ULONG Length;
	ARC_STATUS Status = OfpGetPropLen(Handle, Name, &Length);
	if (ARC_FAIL(Status)) return Status;

	static BYTE s_PropReg[0x1000];
	if (Length >= 0x1000) {
		// sorry what
		return _E2BIG;
	}

	ULONG ReadLength = Length;
	Status = OfpGetProp(Handle, Name, s_PropReg, &ReadLength);
	if (ReadLength != Length) return _E2BIG;

	OFHANDLE Parent = OfParent(Handle);
	ULONG AddressCells = 0, SizeCells = 0;
	Status = OfGetPropInt(Parent, "#address-cells", &AddressCells);
	if (ARC_FAIL(Status)) AddressCells = 2;
	Status = OfGetPropInt(Parent, "#size-cells", &SizeCells);
	if (ARC_FAIL(Status) || SizeCells == 0) SizeCells = 1;

	ULONG LocalCount = Length / ((AddressCells + SizeCells) * sizeof(ULONG));
	if (Register == NULL || *Count == 0) {
		*Count = LocalCount;
		return _ESUCCESS;
	}

	if (*Count < LocalCount) LocalCount = *Count;
	else *Count = LocalCount;

	PULONG pReg = (PULONG)&s_PropReg[0];

	for (ULONG i = 0; i < LocalCount; i++) {
		// Copy the physical address base.
		if (AddressCells == 1) {
			memcpy(&Register[i]. PhysicalAddress.HighPart, &pReg[0], sizeof(*pReg));
			Register[i].PhysicalAddress.LowPart = Register[i].PhysicalAddress.HighPart;
		}
		else {
			memcpy(&Register[i].PhysicalAddress.HighPart, &pReg[0], sizeof(*pReg));
			memcpy(&Register[i].PhysicalAddress.LowPart, &pReg[AddressCells - 1], sizeof(*pReg));
		}
		// And copy the size.
		memcpy(&Register[i].Size, &pReg[AddressCells + SizeCells - 1], sizeof(*pReg));

		pReg = &pReg[AddressCells + SizeCells];
	}

	return _ESUCCESS;
}

/// <summary>
/// Gets a null-terminated string property.
/// </summary>
/// <param name="Handle">OF device handle</param>
/// <param name="Name">Property name</param>
/// <param name="Buffer">Buffer to write property value into (if NULL, just gets length)</param>
/// <param name="BufLen">Reference to length, returns actual length of property value (if 0, just gets length)</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfGetPropString(OFHANDLE Handle, const char* Name, char* Buffer, PULONG Length) {
	if (Length == NULL) return _EINVAL;
	ULONG LocalLength = *Length;
	if (Buffer != NULL && LocalLength != 0) {
		memset(Buffer, 0, LocalLength);
	}
	ARC_STATUS Status = OfGetProperty(Handle, Name, Buffer, &LocalLength);
	if (ARC_FAIL(Status)) return Status;
	*Length = LocalLength + 1;
	return _ESUCCESS;
}

/// <summary>
/// Opens a device from an Open Firmware device path.
/// </summary>
/// <param name="Name">Device path</param>
/// <returns>Device instance handle, or OFINULL on error</returns>
OFIHANDLE OfOpen(const char* Name) {
	if (Name == NULL) return OFINULL;

	struct {
		OF_CALL_HEADER Header;
		const char* Name;
		OFIHANDLE Handle;
	} args = {
		{ "open", 1, 1 },
		Name,
		0
	};

	if (!OF_CALL(args)) return OFINULL;
	return args.Handle;
}

/// <summary>
/// Closes an open device instance handle.
/// </summary>
/// <param name="Handle">Device instance handle</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfClose(OFIHANDLE Handle) {
	if (Handle == OFINULL) return _EINVAL;

	struct {
		OF_CALL_HEADER Header;
		OFIHANDLE Handle;
	} args = {
		{ "close", 1, 0 },
		Handle
	};

	if (!OF_CALL(args)) return _EFAULT;
	return _ESUCCESS;
}

/// <summary>
/// Reads from an open Open Firmware device
/// </summary>
/// <param name="Handle">Device instance handle</param>
/// <param name="Buffer">Buffer to read to</param>
/// <param name="Length">Length to read</param>
/// <param name="Count">On success returns actual length read</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfRead(OFIHANDLE Handle, PVOID Buffer, ULONG Length, PULONG Count) {
	if (Handle == OFINULL || Buffer == NULL || Count == NULL) return _EINVAL;

	struct {
		OF_CALL_HEADER Header;
		OFIHANDLE Handle;
		PVOID Buffer;
		ULONG Length;
		ULONG Count;
	} args = {
		{ "read", 3, 1 },
		Handle,
		Buffer,
		Length,
		0
	};

	if (!OF_CALL(args)) return _EFAULT;
	if (args.Count == 0xFFFFFFFFul) *Count = 0;
	else *Count = args.Count;
	return _ESUCCESS;
}

/// <summary>
/// Writes to an open Open Firmware device
/// </summary>
/// <param name="Handle">Device instance handle</param>
/// <param name="Buffer">Buffer to write from</param>
/// <param name="Length">Length to write</param>
/// <param name="Count">On success returns actual length written</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfWrite(OFIHANDLE Handle, const void* Buffer, ULONG Length, PULONG Count) {
	if (Handle == OFINULL || Buffer == NULL || Count == NULL) return _EINVAL;

	struct {
		OF_CALL_HEADER Header;
		OFIHANDLE Handle;
		const void* Buffer;
		ULONG Length;
		ULONG Count;
	} args = {
		{ "write", 3, 1 },
		Handle,
		Buffer,
		Length,
		0
	};

	if (!OF_CALL(args)) return _EFAULT;
	if (args.Count == 0xFFFFFFFFul) *Count = 0;
	else *Count = args.Count;
	return _ESUCCESS;
}

/// <summary>
/// Seeks to an offset in an open Open Firmware device
/// </summary>
/// <param name="Handle">Device instance handle</param>
/// <param name="Position">Position to seek to</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfSeek(OFIHANDLE Handle, int64_t Position) {
	if (Handle == OFINULL) return _EINVAL;

	struct {
		OF_CALL_HEADER Header;
		OFIHANDLE Handle;
		LARGE_INTEGER_BIG Position;
		ULONG Result;
	} args = {
		{ "seek", 3, 1 },
		Handle,
		{ .QuadPart = Position },
		0
	};

	if (!OF_CALL(args)) return _EFAULT;
	//if (args.Result == 0xFFFFFFFFul) return _ESUCCESS;
	return _ESUCCESS;
}

/// <summary>
/// Allocates a range of physical memory
/// </summary>
/// <param name="PhysicalAddress">Physical address to allocate</param>
/// <param name="Size">Length to allocate</param>
/// <param name="Alignment">Address alignment</param>
/// <returns>Allocated physical address, or NULL on failure</returns>
PVOID OfClaim(PVOID PhysicalAddress, ULONG Size, ULONG Alignment) {
	struct {
		OF_CALL_HEADER Header;
		PVOID PhysicalAddress;
		ULONG Size;
		ULONG Alignment;
		PVOID Return;
	} args = {
		{ "claim", 3, 1 },
		PhysicalAddress,
		Size,
		Alignment,
		NULL
	};

	if (!OF_CALL(args)) return NULL;
	if ((ULONG)args.Return == 0xFFFFFFFFul) return NULL;
	return args.Return;
}

/// <summary>
/// Frees a range of physical memory
/// </summary>
/// <param name="Address">Physical adddress to free</param>
/// <param name="Size">Length of memory chunk to free</param>
void OfRelease(PVOID Address, ULONG Size) {
	struct {
		OF_CALL_HEADER Header;
		PVOID Address;
		ULONG Size;
	} args = {
		{ "release", 2, 0 },
		Address,
		Size
	};

	OF_CALL(args);
}

/// <summary>
/// Calls a Forth method on an Open Firmware device.
/// </summary>
/// <param name="NumIn">Number of input parameters</param>
/// <param name="NumOut">Number of output parameters</param>
/// <param name="OutArgs">Optional buffer to store output parameters</param>
/// <param name="FuncName">Name of function to call</param>
/// <param name="Handle">Device handle to call function on</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfCallMethod(ULONG NumIn, ULONG NumOut, POF_ARGUMENT OutArgs, const char* FuncName, OFIHANDLE Handle, ...) {
	if ((NumIn > 16) || (NumOut > 16) || ((NumIn + NumOut) > 16)) return _EINVAL;

	va_list va;
	va_start(va, Handle);

	struct {
		OF_CALL_HEADER Header;
		const char* FuncName;
		OFIHANDLE Handle;
		OF_ARGUMENT Args[17];
	} args = {
		{ "call-method", NumIn + 2, NumOut + 1 },
		FuncName,
		Handle,
		{{0}}
	};

	for (ULONG i = 0; i < NumIn; i++) {
		args.Args[NumIn - i - 1].Int = va_arg(va, ULONG);
	}

	va_end(va);

	if (!OF_CALL(args)) return _EFAULT;
	if (args.Args[NumIn].Int != 0) return _EFAULT;
	if (OutArgs != NULL) {
		for (ULONG i = 0; i < NumOut; i++) {
			OutArgs[i] = args.Args[NumIn + 1 + NumOut - 1 - i];
		}
	}

	return _ESUCCESS;
}

/// <summary>
/// Execute an arbitrary Forth command.
/// </summary>
/// <param name="NumIn">Number of input parameters</param>
/// <param name="NumOut">Number of output parameters</param>
/// <param name="OutArgs">Optional buffer to store output parameters</param>
/// <param name="FuncName">Name of function to call</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfInterpret(ULONG NumIn, ULONG NumOut, POF_ARGUMENT OutArgs, const char* FuncName, ...) {
	if ((NumIn > 16) || (NumOut > 16) || ((NumIn + NumOut) > 16)) return _EINVAL;

	va_list va;
	va_start(va, FuncName);

	struct {
		OF_CALL_HEADER Header;
		const char* FuncName;
		OF_ARGUMENT Args[17];
	} args = {
		{ "interpret", NumIn + 1, NumOut + 1 },
		FuncName,
		{{0}}
	};

	for (ULONG i = 0; i < NumIn; i++) {
		args.Args[NumIn - i - 1].Int = va_arg(va, ULONG);
	}

	va_end(va);

	if (!OF_CALL(args)) return _EFAULT;
	if (args.Args[NumIn].Int != 0) return _EFAULT;
	if (OutArgs != NULL) {
		for (ULONG i = 0; i < NumOut; i++) {
			OutArgs[i] = args.Args[NumIn + 1 + NumOut - 1 - i];
		}
	}

	return _ESUCCESS;
}

/// <summary>
/// Returns a number representing the passage of time in milliseconds
/// </summary>
/// <returns>Timer value</returns>
ULONG OfMilliseconds(void) {
	struct {
		OF_CALL_HEADER Header;
		ULONG Ret;
	} args = {
		{ "milliseconds", 0, 1 },
		0
	};

	if (!OF_CALL(args)) return 0;
	return args.Ret;
}

/// <summary>
/// Reboots the system, booting from the provided path.
/// </summary>
/// <param name="BootSpec">Boot path</param>
void OfBoot(const char* BootSpec) {
	struct {
		OF_CALL_HEADER Header;
		const char* BootSpec;
	} args = {
		{ "boot", 1, 0 },
		BootSpec
	};

	OF_CALL(args);
}

/// <summary>
/// Drops to the Open Firmware command line. Execution can be later resumed.
/// </summary>
void OfEnter(void) {
	OfpVoidCall("enter");
}

/// <summary>
/// Exits the OF application.
/// </summary>
void OfExit(void) {
	OfpVoidCall("exit");
}