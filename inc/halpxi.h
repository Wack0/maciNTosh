// PXI public API
#pragma once

typedef void (*PADB_DATA_CALLBACK)(UCHAR Status, UCHAR Command, PUCHAR Data, ULONG Length);

// Send a command over PXI and receive the response.
// Command: command to send
// Arguments: pointer to additional arguments (can be NULL)
// ArgLength: length of Arguments buffer
// VariadicIn: whether PXI expects a length byte after Command
// Response: pointer to received response (can be NULL)
// ResponseLength: length of Response buffer
// VariadicOut: whether PXI sends a length byte before the response
// Returns actual received response length.
NTHALAPI UCHAR HalPxiCommand(UCHAR Command, PUCHAR Arguments, UCHAR ArgLength, BOOLEAN VariadicIn, PUCHAR Response, UCHAR ResponseLength, BOOLEAN VariadicOut);

// HalPxiCommand where no response is received
NTHALAPI void HalPxiCommandVoid(UCHAR Command, PUCHAR Arguments, UCHAR ArgLength, BOOLEAN VariadicIn);

// HalPxiCommandVoid, where no arguments are sent
NTHALAPI void HalPxiCommandSimple(UCHAR Command);

// Send ADB command, returns false if length too big
NTHALAPI BOOLEAN HalPxiCommandAdb(UCHAR Command, PUCHAR Data, UCHAR Length, BOOLEAN Poll);

// Set ADB command callback
NTHALAPI void HalPxiAdbSetCallback(PADB_DATA_CALLBACK Callback);

// Set ADB autopoll bitmask
NTHALAPI void HalPxiAdbAutopoll(USHORT Mask);

