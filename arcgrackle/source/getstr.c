#include <stdio.h>
#include <string.h>
#include "getstr.h"
#include "timer.h"

//static
void WaitMs(ULONG milliseconds) {
	mdelay(milliseconds);
}

/// <summary>
/// Reads a string from the keyboard until \n, ESC, or StringLength.
/// </summary>
/// <param name="String">Buffer where the string will be placed.</param>
/// <param name="StringLength">Length of buffer.</param>
/// <param name="InitialString">Optional initial string</param>
/// <param name="CurrentRow">Current screen row</param>
/// <param name="CurrentColumn">Current screen column</param>
/// <returns>Success or Escape or Up/Down arrow enumeration</returns>
GETSTRING_ACTION KbdGetString(PCHAR String, ULONG StringLength, PCHAR InitialString, ULONG CurrentRow, ULONG CurrentColumn) {
	PCHAR Buffer;

	if (InitialString) {
		snprintf(String, StringLength, "%s", InitialString);
		Buffer = String + strlen(String);
	}
	else {
		*String = 0;
		Buffer = String;
	}

	PCHAR Cursor = Buffer;
	bool Cr = false;
	GETSTRING_ACTION Action = GetStringMaximum;

	while (Action == GetStringMaximum) {
		Cr = false;

		// Print the string.
		ArcSetPosition(CurrentRow, CurrentColumn);
		printf("%s   ", String);
		//printf("%s", "\x9B\x32K");

		// Print the cursor.
		ArcSetScreenAttributes(true, false, true);
		ArcSetPosition(CurrentRow, (ULONG)(Cursor - String) + CurrentColumn);
		if (Cursor >= Buffer) printf(" ");
		else printf("%c", Cursor[0]);
		ArcSetScreenAttributes(true, false, false);

		while (!IOSKBD_CharAvailable()) {
			// no operation
		}

		UCHAR Character = IOSKBD_ReadChar();

		if ((ULONG)(Buffer - String) == StringLength) {
			Action = GetStringEscape;
			break;
		}

		switch (Character) {
		case '\x1b':
		{
			bool IsControl = false;
			// Check if this is a control sequence.
			WaitMs(10);
			if (IOSKBD_CharAvailable()) {
				Character = IOSKBD_ReadChar();
				if (Character == '[') {
					IsControl = true;
				}
			}

			if (!IsControl) {
				Action = GetStringEscape;
				break;
			}
		}
		// fall through
		case '\x9b':
			Character = IOSKBD_ReadChar();
			switch (Character) {
			case 'A': // up arrow
				Action = GetStringUpArrow;
				break;
			case 'B': // down arrow
				Action = GetStringDownArrow;
				break;
			case 'D': // left arrow
				if (Cursor != String) Cursor--;
				continue;
			case 'C': // right arrow
				if (Cursor != Buffer) Cursor++;
				continue;
			case 'H': // home key
				Cursor = String;
				continue;
			case 'K': // end key
				Cursor = Buffer;
				continue;
			case 'P': // delete key
				strcpy(Cursor, Cursor + 1);
				if (Buffer != String) Buffer--;
				continue;
			default:
				break;
			}
			break;
		case '\r': // cr
		case '\n': // lf
			Cr = true;
			Action = GetStringSuccess;
			break;
		case '\b': // backspace
			if (Cursor != String) Cursor--;
			strcpy(Cursor, Cursor + 1);
			if (Buffer != String) Buffer--;
			break;
		default:
			// Store the char.
			Buffer++;
			if (Buffer > Cursor) {
				PCHAR pCopy = Buffer;
				while (pCopy != Cursor) {
					pCopy--;
					pCopy[1] = pCopy[0];
				}
			}
			*Cursor = Character;
			Cursor++;
			break;
		}
	}

	// Clear the cursor.
	ArcSetPosition(CurrentRow, (ULONG)(Cursor - String) + CurrentColumn);
	if (Cursor >= Buffer) printf(" ");
	else printf("%c", Cursor[0]);

	if (Cr) printf("\n");

	// If not successful, return an empty string.
	if (Action != GetStringSuccess) *String = 0;
	return Action;
}