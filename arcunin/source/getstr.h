#pragma once
#include "arc.h"

typedef enum _GETSTRING_ACTION {
    GetStringSuccess,
    GetStringEscape,
    GetStringUpArrow,
    GetStringDownArrow,
    GetStringMaximum
} GETSTRING_ACTION, * PGETSTRING_ACTION;

GETSTRING_ACTION KbdGetString(PCHAR String, ULONG StringLength, PCHAR InitialString, ULONG CurrentRow, ULONG CurrentColumn);
