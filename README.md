# Windows NT for Power Macintosh

This repository currently contains the source code for the ARC firmware and its loader, targeting New World Power Macintosh systems using the *Gossamer* architecture (that is, MPC106 "Grackle" memory controller and PCI host, and "Heathrow" or "Paddington" super-I/O chip on the PCI bus). That is, the following systems:

* iMac G3 (tray-loading)
* Power Macintosh G3 (Blue & White) *"Yosemite"*
* Macintosh PowerBook G3 Bronze Keyboard *"Lombard"* 
* Power Macintosh G4 PCI *"Yikes!"*

The ARC firmware itself runs at a low enough level that it should be compatible with Old World systems using the same chipset too, but there is currently no loader for these systems; these are the following:

* Power Macintosh G3 (beige)
* Macintosh PowerBook G3 Series *"Wallstreet"*, *"PDQ"*

There may be issues on your hardware; with real hardware, this has only been tested on a Lombard.

NT HAL and drivers have no source present for now.

## Drivers present in ARC firmware

* Cuda and PMU (albeit Cuda is untested on real hardware)
	* ADB keyboard
* Flat 32bpp video framebuffer, set up by the loader. Currently the loader only supports ATI hardware (there may be issues with any ATI hardware with fcode version prior to 1.69, only the ATI Rage Pro LT (as present in Lombard) has been tested)
* Mac I/O internal IDE controllers, forked from OpenBIOS (**there are no drivers for PCI IDE controllers!**)
* USB OHCI forked from OpenBIOS (**broken, nonworking, and initialisation code commented out**)

## Drivers currently done for NT

* HAL, including: NT boot time framebuffer, super I/O interrupt controller, Grackle PCI bus support, Cuda and PMU (including low level ADB), serial port for kernel debugging only
	* (please note Cuda support is currently untested on real hardware)
* Mac I/O internal IDE controller, forked from `atapi.sys` from NT4 DDK
* General HID/storage driver, intended to also contain a USB stack in future but currently only implements ADB keyboard/mouse and ramdisk as floppy drive for installing drivers at text setup time
* Flat 32bpp video framebuffer miniport driver

## Software compatibility

NT4 only, currently. NT 3.51 may become compatible if HAL and drivers get ported to it. NT 3.5 will never be compatible, as it only supports PowerPC 601.
(The additional suspend/hibernation features in NT 3.51 PMZ could be made compatible in theory but in practise would require all of the additional drivers for that to be reimplemented.)

## Installing

### Preliminary

* Grab binaries from the releases page. Burn the image to optical media.

### Partitioning Disk

* Boot your PowerMac from the burned optical media. When you get to ARC firmware menu, go to `Run firmware setup`, then `Repartition disk for NT installation`.
* The disk partitioner will first let you enter partition size of the NT partition (up to the 16383x16x63 CHS limit, minus 32 MB ARC system partition + 1 MB for partition tables / MBR backup / OS 9 drivers / ARC environment variable storage, giving a maximum possible size of 8030 MB), then will drop to a menu allowing the creation of additional Mac partitions.
	* After adding a partition to the list, the only way to remove from the list is by cancelling the operation and starting the partitioner again.
* After you have created all Mac partitions you want, choose `Finish partitioning and install`, and confirm the operation.
* When finished, the partitioner will ask to `Press any key to restart`. Do so, and boot your PowerMac from the CD again.

### Installing NT

* Eject CD and insert your NT4 CD.
* Go to `Run a program` and enter the path `cd:\ppc\setupldr` - this may be `cd01:` or `cd02:` (...) if you have multiple optical drives present on your system.
	* This may error with `The file or device does not exist`, just go back to `Run a program` and try again if so. 
* NT4 setupldr will start.
	* You will receive the message `Setup could not determine the type of computer you have`.
	* Choose `Other` (default selected option), just press `Enter` when asked for hardware support disk.
	* Pick your system from the list - all are equivalent and will load the Gossamer chipset HAL `halgoss`.
* Next you will receive the message `Setup could not determine the type of one or more mass storage drivers installed in your system`. Two drivers need to be loaded at this point:
	* press `S` to pick a driver, choose `Other` from the list, press `Enter` when asked for hardware support disk
	* Choose the first driver `Mac I/O IDE Controller`
	* follow the previous steps again, but this time choose the second driver `PowerMac General HID & Storage`
	* finally, press Enter to continue
* You will receive the message `Setup could not determine the type of video adapter installed in the system`. Choose `Other` from the list, press `Enter` when asked for hardware support disk, and choose the only option `Open Firmware Frame Buffer`.
* NT will boot and text setup will start. Go through the text setup.
* Under `Setup has determined that your computer contains the following hardware and software components`, change `Keyboard` from `Unknown` to `XT, AT or Enhanced Keyboard (83-104 keys)` and `Pointing Device` from `Unknown` to `No Mouse or Other Pointing Device`.
* Choose the `C:` drive from the partition list. If you chose to create an NT partition of size 2GB or less, it must be formatted.
* If you chose to create an NT partition of over 2GB in size, `chkdsk` will find errors and require a reboot. Boot your PowerMac from the ARC firmware CD again and follow the steps to boot the NT4 text setup again.
* Proceed through the rest of NT text and graphical setup as normal.

## Known issues

* If you are looking for a stable operating system, this is not it. Expect bugchecks, expect graphical setup to fail and restart because of bugchecks, etc.
	* On a laptop system you may wish to remove the battery. At least on Lombard, the only way to power off the system when it bugchecks is via PMU reset or via total power removal.
* Currently the implemented drivers are the bare minimum to run and use NT.
* I have observed PMU hard shutdowns on NT boot, fixed only by a PMU reset. No idea what caused this.

## Dualboot quirks

If you create additional Mac partitions, please make note of the following:
* The Mac partitions are listed in the partition table as HFS partitions but are not formatted. Use Disk Utility from OS X 10.1 or above to format the partitions. (Erase the **volumes**, not the **drive**!)
* The OS X installer, and just booting OS 8/OS 9, will error if a valid MBR is present on the disk at all, which is required for NT. In ARC firmware, go to `Run firmware setup` then `Reboot to OSX install or OS8/OS9` if you wish to boot to those listed operating systems.
	* Booting back to the ARC firmware will fix the MBR, so be sure to always use this option when unsure.
	* In particular, formatting the created HFS partitions in OS X 10.2 and 10.3 will not work when a valid MBR is present!
* To allow OS 9 to mount the hard disk, boot from an OS 9 CD, run Drive Setup, select the drive and use the `Update Driver` option from the `Functions` menu.

## Building ARC firmware

You need devkitPPC. Additionally, a `libgcc.a` compiled for `powerpcle` must be present in `arcgrackle/gccle`. If you need to find one, it should be present on any Void Linux mirror, the current filename to search for as of 2024-07-12 is `cross-powerpcle-linux-gnu-0.34_1.x86_64.xbps` - decompress it by `zstdcat cross-powerpcle-linux-gnu-0.34_1.x86_64.xbps -o cross-powerpcle-linux-gnu-0.34_1.x86_64.tar`, then pull the file out of the tarball: `usr/lib/gcc/powerpcle-linux-gnu/10.2/libgcc.a`.

* Ensure `DEVKITPPC` environment variable is set to your devkitPPC directory, usually `/opt/devkitpro/devkitPPC`
* Build the big endian libc: `cd baselibc ; make ; cd ..`
* Build the ARC firmware loader: `cd arcloader_grackle ; make ; cd ..`
* Build the little endian libc: `cd arcgrackle/baselibc ; make ; cd ../..`
* Build the ARC firmware itself: `cd arcgrackle ; make ; cd ..`

Replace `stage1.elf` and `stage2.elf` inside the release image. For recreating the image from a folder dump, use your preferred tool to create a hybrid HFS+ISO image, make sure `System` folder is blessed and `BootX` file is of type `tbxi`.

Please note that `stage1.elf` must not be larger than 16KB and `stage2.elf` must not be larger than 224KB.

## Acknowledgements

* libc used is [baselibc](https://github.com/PetteriAimonen/Baselibc)
* ELF loader and makefiles adapted from [The Homebrew Channel](https://github.com/fail0verflow/hbc)
* Some lowlevel powerpc stuff, and ARC firmware framebuffer console implementation and font, adapted from [libogc](https://github.com/devkitPro/libogc)
* Some ARC firmware drivers (IDE, USB) adapted from [OpenBIOS](https://github.com/openbios/openbios)
	* USB drivers in OpenBIOS were themselves adapted from [coreboot](https://github.com/coreboot/coreboot)
* ISO9660 FS implementation inside ARC firmware is [lib9660](https://github.com/erincandescent/lib9660) with some modifications.
* FAT FS implementation inside ARC firmware is [Petit FatFs](http://elm-chan.org/fsw/ff/00index_p.html) with some modifications.
