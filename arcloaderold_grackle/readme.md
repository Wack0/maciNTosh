# ARC firmware Old World bootloader for Gossamer/Wallstreet

This is the stage 1 ARC firmware bootloader for Old World systems using the MPC106 PCI host controller.

The toolchain used to build this is [Retro68](https://github.com/autc04/Retro68), with included patch (to change the libhfs 800KB restriction to 8KB which is the actual lower bound for an HFS partition image).

Included in the `apple` folder is:
- `boot1.bin`: m68k HFS boot block with minor patches (to abort boot if Escape button is held, added because I had issues under emulation with boot-time key-combinations not working)
- `boot2stub.bin`: second stage bootloader stub, contains a single MixedMode routine descriptor to call into PowerPC code (the built PEF)

Run `make ; make addstage2` after building `arcgrackle` to create a `stage1.img` HFS partition image used for booting Old World systems, that can be injected into a standard ISO-9660 image using `OldWorldIsoBuilder`.
