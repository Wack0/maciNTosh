#pragma once
#include <stdbool.h>
#include "arc.h"

typedef ULONG OFHANDLE, *POFHANDLE;
typedef ULONG OFIHANDLE, *POFIHANDLE;

#define OFNULL (OFHANDLE)0
#define OFINULL (OFIHANDLE)0

typedef struct _OF_CALL_HEADER {
	const char* Name;
	ULONG NumIn;
	ULONG NumOut;
} OF_CALL_HEADER, * POF_CALL_HEADER;

typedef int (*tfpOpenFirmwareCall)(POF_CALL_HEADER Args);

typedef struct _OF_NEXT_NAME {
	char Name[32];
} OF_NEXT_NAME, *POF_NEXT_NAME;

typedef union _OF_ARGUMENT {
	ULONG Int;
	PVOID Pointer;
	OFHANDLE Handle;
	OFIHANDLE Instance;
} OF_ARGUMENT, *POF_ARGUMENT;

typedef struct _OF_REGISTER {
	LARGE_INTEGER_BIG PhysicalAddress;
	ULONG Size;
} OF_REGISTER, *POF_REGISTER;

typedef struct _OF_INTERRUPT {
	ULONG Interrupt;
	ULONG Unknown;
} OF_INTERRUPT, *POF_INTERRUPT;

/// <summary>
/// Initialises the OF interface
/// </summary>
/// <param name="OfCall">Function pointer to the OF client interface.</param>
/// <returns>True on success, false on error</returns>
bool OfInit(tfpOpenFirmwareCall OfCall);

/// <summary>
/// Gets the component handle for /chosen - guaranteed to be present
/// </summary>
/// <returns>Component handle</returns>
OFHANDLE OfGetChosen(void);

/// <summary>
/// Gets the peer of the current component, OF equivalent to ARC GetPeer
/// </summary>
/// <param name="Handle">Component to obtain the peer of</param>
/// <returns>Peer component</returns>
OFHANDLE OfPeer(OFHANDLE Handle);

/// <summary>
/// Gets the child of the current component, OF equivalent to ARC GetChild
/// </summary>
/// <param name="Component">Component to obtain the child of. If null, obtains the root component.</param>
/// <returns>Component</returns>
OFHANDLE OfChild(OFHANDLE Handle);

/// <summary>
/// Gets the parent of the current component, OF equivalent to ARC GetParent
/// </summary>
/// <param name="Component">Component to obtain the parent of.</param>
/// <returns>Parent component</returns>
OFHANDLE OfParent(OFHANDLE Handle);

/// <summary>
/// Gets OF path name from component handle.
/// </summary>
/// <param name="Handle">Component to obtain the path name of</param>
/// <param name="Buffer">Buffer where the path name is written</param>
/// <param name="Length">Reference to length of buffer, entire length (without null terminator) written here</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfPackageToPath(OFHANDLE Handle, PCHAR Buffer, PULONG Length);

/// <summary>
/// Gets OF path name from device handle.
/// </summary>
/// <param name="Handle">Device to obtain the path name of</param>
/// <param name="Buffer">Buffer where the path name is written</param>
/// <param name="Length">Reference to length of buffer, entire length (without null terminator) written here</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfInstanceToPath(OFIHANDLE Handle, PCHAR Buffer, PULONG Length);

/// <summary>
/// Gets OF component handle from device handle.
/// </summary>
/// <param name="Handle">Device to obtain component handle of</param>
/// <returns>Component handle or OFNULL on error</returns>
OFHANDLE OfInstanceToPackage(OFIHANDLE Handle);

/// <summary>
/// Gets a property value of an Open Firmware device.
/// </summary>
/// <param name="Handle">OF device handle</param>
/// <param name="Name">Property name</param>
/// <param name="Buffer">Buffer to write property value into (if NULL, just gets length)</param>
/// <param name="BufLen">Reference to length, returns actual length of property value (if 0, just gets length)</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfGetProperty(OFHANDLE Handle, const char* Name, void* Buffer, PULONG BufLen);

/// <summary>
/// Gets the next property name of an Open Firmware device
/// </summary>
/// <param name="Handle">OF device handle</param>
/// <param name="CurrentName">Current property name</param>
/// <param name="NextName">Buffer to write next property name to</param>
/// <returns>ARC status code: EINVAL for invalid arguments, EFAULT if call failed, ENOENT if no more properties</returns>
ARC_STATUS OfGetNextProperty(OFHANDLE Handle, const char* CurrentName, POF_NEXT_NAME NextName);

/// <summary>
/// Sets a property value of an Open Firmware device
/// </summary>
/// <param name="Handle">OF device handle</param>
/// <param name="Name">Name of property to set</param>
/// <param name="Buffer">Buffer containing value to set</param>
/// <param name="BufLen">Reference to length of value, on success set to actual length</param>
/// <returns>ARC status code.</returns>
ARC_STATUS OfSetProperty(OFHANDLE Handle, const char* Name, PVOID Buffer, PULONG BufLen);

/// <summary>
/// Gets a device handle from an Open Firmware device path.
/// </summary>
/// <param name="Name">Device path</param>
/// <returns>Device handle, or OFNULL on error.</returns>
OFHANDLE OfFindDevice(const char* Name);

/// <summary>
/// Determines if a device property exists.
/// </summary>
/// <param name="Handle">Device handle</param>
/// <param name="Name">Property name</param>
/// <returns>True if exists, false if not</returns>
bool OfPropExists(OFHANDLE Handle, const char* Name);

/// <summary>
/// Gets an integer property.
/// </summary>
/// <param name="Handle">Device handle</param>
/// <param name="Name">Property name</param>
/// <param name="Value">Value written here on success</param>
/// <returns>ARC status code.</returns>
ARC_STATUS OfGetPropInt(OFHANDLE Handle, const char* Name, ULONG* Value);

/// <summary>
/// Gets a register property.
/// </summary>
/// <param name="Handle">Device handle</param>
/// <param name="Name">Property name</param>
/// <param name="Index">Register index</param>
/// <param name="Register">Value written here on success</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfGetPropReg(OFHANDLE Handle, const char* Name, ULONG Index, POF_REGISTER Register);

/// <summary>
/// Gets all registers of a register property.
/// </summary>
/// <param name="Handle">Device handle</param>
/// <param name="Name">Property name</param>
/// <param name="Register">Buffer where values are written, can be NULL</param>
/// <param name="Count">Number of registers</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfGetPropRegs(OFHANDLE Handle, const char* Name, POF_REGISTER Register, PULONG Count);

/// <summary>
/// Gets a null-terminated string property.
/// </summary>
/// <param name="Handle">OF device handle</param>
/// <param name="Name">Property name</param>
/// <param name="Buffer">Buffer to write property value into (if NULL, just gets length)</param>
/// <param name="BufLen">Reference to length, returns actual length of property value (if 0, just gets length)</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfGetPropString(OFHANDLE Handle, const char* Name, char* Buffer, PULONG Length);

/// <summary>
/// Opens a device from an Open Firmware device path.
/// </summary>
/// <param name="Name">Device path</param>
/// <returns>Device instance handle, or OFINULL on error</returns>
OFIHANDLE OfOpen(const char* Name);

/// <summary>
/// Closes an open device instance handle.
/// </summary>
/// <param name="Handle">Device instance handle</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfClose(OFIHANDLE Handle);

/// <summary>
/// Reads from an open Open Firmware device
/// </summary>
/// <param name="Handle">Device instance handle</param>
/// <param name="Buffer">Buffer to read to</param>
/// <param name="Length">Length to read</param>
/// <param name="Count">On success returns actual length read</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfRead(OFIHANDLE Handle, PVOID Buffer, ULONG Length, PULONG Count);

/// <summary>
/// Writes to an open Open Firmware device
/// </summary>
/// <param name="Handle">Device instance handle</param>
/// <param name="Buffer">Buffer to write from</param>
/// <param name="Length">Length to write</param>
/// <param name="Count">On success returns actual length written</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfWrite(OFIHANDLE Handle, const void* Buffer, ULONG Length, PULONG Count);

/// <summary>
/// Seeks to an offset in an open Open Firmware device
/// </summary>
/// <param name="Handle">Device instance handle</param>
/// <param name="Position">Position to seek to</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfSeek(OFIHANDLE Handle, int64_t Position);

/// <summary>
/// Allocates a range of physical memory
/// </summary>
/// <param name="PhysicalAddress">Physical address to allocate</param>
/// <param name="Size">Length to allocate</param>
/// <param name="Alignment">Address alignment</param>
/// <returns>Allocated physical address, or NULL on failure</returns>
PVOID OfClaim(PVOID PhysicalAddress, ULONG Size, ULONG Alignment);

/// <summary>
/// Frees a range of physical memory
/// </summary>
/// <param name="Address">Physical adddress to free</param>
/// <param name="Size">Length of memory chunk to free</param>
void OfRelease(PVOID Address, ULONG Size);

/// <summary>
/// Calls a Forth method on an Open Firmware device.
/// </summary>
/// <param name="NumIn">Number of input parameters</param>
/// <param name="NumOut">Number of output parameters</param>
/// <param name="OutArgs">Optional buffer to store output parameters</param>
/// <param name="FuncName">Name of function to call</param>
/// <param name="Handle">Device handle to call function on</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfCallMethod(ULONG NumIn, ULONG NumOut, POF_ARGUMENT OutArgs, const char* FuncName, OFIHANDLE Handle, ...);

/// <summary>
/// Execute an arbitrary Forth command.
/// </summary>
/// <param name="NumIn">Number of input parameters</param>
/// <param name="NumOut">Number of output parameters</param>
/// <param name="OutArgs">Optional buffer to store output parameters</param>
/// <param name="FuncName">Name of function to call</param>
/// <returns>ARC status code</returns>
ARC_STATUS OfInterpret(ULONG NumIn, ULONG NumOut, POF_ARGUMENT OutArgs, const char* FuncName, ...);

/// <summary>
/// Returns a number representing the passage of time in milliseconds
/// </summary>
/// <returns>Timer value</returns>
ULONG OfMilliseconds(void);

/// <summary>
/// Reboots the system, booting from the provided path.
/// </summary>
/// <param name="BootSpec">Boot path</param>
void OfBoot(const char* BootSpec);

/// <summary>
/// Drops to the Open Firmware command line. Execution can be later resumed.
/// </summary>
void OfEnter(void);

/// <summary>
/// Exits the OF application.
/// </summary>
void OfExit(void);
