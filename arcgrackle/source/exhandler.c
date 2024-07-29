#include <stddef.h>
#include <string.h>
#include "types.h"
#include "arc.h"
#include "arcconsole.h"

typedef struct _REGISTER_DUMP {
    ULONG gpr[32];
    //ULONG iv;
    ULONG srr0;
    ULONG srr1;
    ULONG cr;
    ULONG lr;
    ULONG ctr;
    ULONG xer;
    ULONG dar;
    ULONG dsisr;
    ULONG sdr1;
} REGISTER_DUMP, * PREGISTER_DUMP;

REGISTER_DUMP RegisterSpace = { 0 };

#define string(s) s, sizeof(s)

static const char* const hexmap = "0123456789ABCDEF";
extern void BugcheckTrampoline(void);
extern void BugcheckHandler(void);

static void PrintHex(ULONG num) {
    char buffer[9];
    buffer[0] = hexmap[(num >> 28) & 0x0F];
    buffer[1] = hexmap[(num >> 24) & 0x0F];
    buffer[2] = hexmap[(num >> 20) & 0x0F];
    buffer[3] = hexmap[(num >> 16) & 0x0F];
    buffer[4] = hexmap[(num >> 12) & 0x0F];
    buffer[5] = hexmap[(num >> 8) & 0x0F];
    buffer[6] = hexmap[(num >> 4) & 0x0F];
    buffer[7] = hexmap[num & 0x0F];
    buffer[8] = ' ';
    ArcConsoleWrite(buffer, 9);
}

void ArcBugcheck(PREGISTER_DUMP regs) {

    ArcConsoleWrite(string("\x9B\x32J\x9b\x31;1HException occurred!\r\nGeneral Purpose Registers:\r\n 00 "));
    PrintHex(regs->gpr[0]);
    PrintHex(regs->gpr[1]);
    PrintHex(regs->gpr[2]);
    PrintHex(regs->gpr[3]);
    PrintHex(regs->gpr[4]);
    PrintHex(regs->gpr[5]);
    PrintHex(regs->gpr[6]);
    PrintHex(regs->gpr[7]);
    ArcConsoleWrite(string("\r\n 08 "));
    PrintHex(regs->gpr[8]);
    PrintHex(regs->gpr[9]);
    PrintHex(regs->gpr[10]);
    PrintHex(regs->gpr[11]);
    PrintHex(regs->gpr[12]);
    PrintHex(regs->gpr[13]);
    PrintHex(regs->gpr[14]);
    PrintHex(regs->gpr[15]);
    ArcConsoleWrite(string("\r\n 16 "));
    PrintHex(regs->gpr[16]);
    PrintHex(regs->gpr[17]);
    PrintHex(regs->gpr[18]);
    PrintHex(regs->gpr[19]);
    PrintHex(regs->gpr[20]);
    PrintHex(regs->gpr[21]);
    PrintHex(regs->gpr[22]);
    PrintHex(regs->gpr[23]);
    ArcConsoleWrite(string("\r\n 24 "));
    PrintHex(regs->gpr[24]);
    PrintHex(regs->gpr[25]);
    PrintHex(regs->gpr[26]);
    PrintHex(regs->gpr[27]);
    PrintHex(regs->gpr[28]);
    PrintHex(regs->gpr[29]);
    PrintHex(regs->gpr[30]);
    PrintHex(regs->gpr[31]);
    ArcConsoleWrite(string("\r\nSpecial Registers:\r\n    %SRR0: "));
    //PrintHex(regs->iv);
    //ArcConsoleWrite(string("  %SRR0: "));
    PrintHex(regs->srr0);
    ArcConsoleWrite(string("  %SRR1: "));
    PrintHex(regs->srr1);
    ArcConsoleWrite(string("\r\n    %CR: "));
    PrintHex(regs->cr);
    ArcConsoleWrite(string("    %LR: "));
    PrintHex(regs->lr);
    ArcConsoleWrite(string("   %CTR: "));
    PrintHex(regs->ctr);
    ArcConsoleWrite(string("   %XER: "));
    PrintHex(regs->xer);
    ArcConsoleWrite(string("\r\n   %DAR: "));
    PrintHex(regs->dar);
    ArcConsoleWrite(string(" %DSISR: "));
    PrintHex(regs->dsisr);
    ArcConsoleWrite(string("  %SDR1: "));
    PrintHex(regs->sdr1);
    ArcConsoleWrite(string("\r\nSystem halted."));
    while (1) {}
}

void ArcBugcheckInit(void) {
    for (ULONG vec = 0x90000000u; vec < 0x90001000u; vec += 0x100) {
        memcpy((PVOID)vec, (PVOID)BugcheckTrampoline, ((ULONG)BugcheckHandler - (ULONG)BugcheckTrampoline));
    }
    __asm__("sync; isync");
}