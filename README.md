# Windows NT for Power Macintosh

This repository currently contains the source code for the ARC firmware and its loader, targeting New World Power Macintosh systems using the *Gossamer* architecture (that is, MPC106 "Grackle" memory controller and PCI host, and "Heathrow" or "Paddington" super-I/O chip on the PCI bus). That is, the following systems:

* iMac G3 (tray-loading)
* Power Macintosh G3 (Blue & White) *"Yosemite"*
* Macintosh PowerBook G3 Bronze Keyboard *"Lombard"* 
* Power Macintosh G4 PCI *"Yikes!"*

The ARC firmware itself runs at a low enough level that it should be compatible with Old World systems using the same chipset too, but there is currently no loader for these systems; these are the following:

* Power Macintosh G3 (beige)
* Macintosh PowerBook G3 Series *"Wallstreet"*, *"PDQ"*

The repository additionally contains the source code for the ARC firmware and its loader, targeting PowerPC Macintosh systems using the *Mac99* architecture (the first iteration of which being the "Uni-North" memory controller and PCI host, and "KeyLargo" super-I/O chip on the PCI bus; later derivatives like "Intrepid" are also supported). That is, the following systems:

* PowerBook G3 Firewire *"Pismo"*
* iBook G3
* iBook G4
** The mid-2005 iBook G4 (`PowerBook6,7`) uses a USB mouse internally and therefore mouse will not work yet.
* PowerBook G4
** The early 2005 and later PowerBook G4s (`PowerBook6,8` and `PowerBook5,6` and later) use a USB keyboard and mouse and are therefore currently not practically supported.

The following systems are theoretically supported, but currently not practically supported due to the lack of USB drivers:

* iMac G3 (slot-loading)
* iMac G4
* Power Macintosh G4 (AGP *"Sawtooth"* and later)

There may be issues on your hardware.

NT HAL and drivers have no source present for now.

## Drivers present in ARC firmware

* Cuda and PMU
	* ADB keyboard
* Flat 32bpp video framebuffer, set up by the loader. Both ATI and nVidia hardware is supported, although nVidia hardware is currently untested.
* Mac I/O internal IDE controllers, forked from OpenBIOS (**there are no drivers for PCI IDE controllers!**)
** The ATA-6 controllers used on some later Mac99 systems (Intrepid, U2) are supported. Please note LBA48 is not yet supported.
* USB OHCI forked from OpenBIOS (**broken, nonworking, and initialisation code commented out**)

## Drivers currently done for NT

* HAL for *Gossamer* chipset, including: NT boot time framebuffer, super I/O interrupt controller, Grackle PCI bus support, Cuda and PMU (including low level ADB), serial port for kernel debugging only
* HAL for *Mac99* chipset, including: NT boot time framebuffer, MPIC interrupt controller, support for all 3 PCI busses on Uni-North (of which one is AGP, but only the PCI subset is supported), PMU (including low level ADB), serial port for kernel debugging only
* Mac I/O internal IDE controllers and ATA-6 controllers, forked from `atapi.sys` from NT4 DDK
* General HID/storage driver, intended to also contain a USB stack in future but currently only implements ADB keyboard/mouse and ramdisk as floppy drive for installing drivers at text setup time
* Flat 32bpp video framebuffer miniport driver

## Software compatibility

NT 3.51 RTM and higher. NT 3.51 betas (build 944 and below) will need kernel patches to run due to processor detection bugs. NT 3.5 will never be compatible, as it only supports PowerPC 601.
(The additional suspend/hibernation features in NT 3.51 PMZ could be made compatible in theory but in practise would require all of the additional drivers for that to be reimplemented.)

## Installing

### Preliminary

* Grab binaries for your system from the releases page.
* For Gossamer/Grackle systems, burn the image to optical media.
	* For Mac99 systems, you can write the image to a USB drive.

### Partitioning Disk

* Boot your PowerMac from the burned optical media.
	* For Mac99 laptops, you can boot to Open Firmware and use the command `probe-usb multi-boot` to show the boot menu with USB device present.
* When you get to ARC firmware menu, go to `Run firmware setup`, then `Repartition disk for NT installation`.
* The disk partitioner will first let you enter partition size of the NT partition (up to the 16383x16x63 CHS limit, minus 32 MB ARC system partition + 1 MB for partition tables / MBR backup / OS 9 drivers / ARC environment variable storage, giving a maximum possible size of 8030 MB), then will drop to a menu allowing the creation of additional Mac partitions.
	* If you choose an NT partition size over 2GB, the partition will be formatted to NTFS.
		* Please be aware that currently, the NTFS version used for formatting is **incompatible with NT 3.51**, so if you want to install NT 3.51, use a partition size that is 2GB or lower.
	* After adding a partition to the list, the only way to remove from the list is by cancelling the operation and starting the partitioner again.
* After you have created all Mac partitions you want, choose `Finish partitioning and install`, and confirm the operation.
* When finished, the partitioner will ask to `Press any key to restart`. Do so, and boot your PowerMac from the CD or USB drive again.

### Installing NT

* For Gossamer/Grackle systems, if ARC firmware does not show `drivers.img ramdisk loaded`, go to `Run firmware setup`, then `Load driver ramdisk` - make sure it succeeds before continuing.
* Eject CD and insert your NT 4 or NT 3.51 CD.
	* For Mac99 systems, the option to eject the CD is in the `Run firmware setup` menu.
* Go to `Run a program` and enter the path `cd:\ppc\setupldr` - this may be `cd01:` or `cd02:` (...) if you have multiple optical drives present on your system.
	* This may error with `The file or device does not exist`, just go back to `Run a program` and try again if so. 
* NT setupldr will start.
	* You will receive the message `Setup could not determine the type of computer you have`.
	* Choose `Other` (default selected option), just press `Enter` when asked for hardware support disk.
	* Pick your system from the list - all are equivalent and will load the correct HAL for your system, which is either the Gossamer chipset HAL `halgoss` or the Mac99 chipset HAL `halunin`.
* Next you will receive the message `Setup could not determine the type of one or more mass storage drivers installed in your system`. Two drivers need to be loaded at this point:
	* press `S` to pick a driver, choose `Other` from the list, press `Enter` when asked for hardware support disk
	* Choose the first driver `Mac I/O IDE Controller`
	* follow the previous steps again, but this time choose the second driver `PowerMac General HID & Storage`
	* finally, press Enter to continue
* You will receive the message `Setup could not determine the type of video adapter installed in the system`. Choose `Other` from the list, press `Enter` when asked for hardware support disk, and choose the correct option depending on the OS you are installing.
	* There are two options in this list; `Open Firmware Frame Buffer` is for NT 4, `Open Firmware Frame Buffer (NT 3.x)` is for NT 3.51.
* NT will boot and text setup will start. Go through the text setup.
* Under `Setup has determined that your computer contains the following hardware and software components`, change `Keyboard` from `Unknown` to `XT, AT or Enhanced Keyboard (83-104 keys)` and `Pointing Device` from `Unknown` to `No Mouse or Other Pointing Device`.
* Choose the `C:` drive from the partition list. If you chose to create an NT partition of size 2GB or less, it must be formatted.
* If you chose to create an NT partition of over 2GB in size, errors will be found by the disk examination process which will require a reboot. You will need to boot back into the ARC firmware from the CD or USB drive and follow the "Installing NT" steps again to get back to this point.
	* On the second attempt, disk examination will succeed, so just choose the `C:` partition again in the NT text setup partition selector.
* Proceed through the rest of NT text and graphical setup as normal.

## Known issues (Grackle/Gossamer)

* On a laptop system you may wish to remove the battery. At least on Lombard, the only way to power off the system when it bugchecks is via PMU reset or via total power removal.
* Currently the implemented drivers are the bare minimum to run and use NT.
* I have observed PMU hard shutdowns on NT boot, fixed only by a PMU reset. No idea what caused this.

## Known issues (Mac99)

* As USB drivers are not working yet, only laptop systems are supported.
* Currently the implemented drivers are the bare minimum to run and use NT.

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
	* For Mac99, use the `arcloader_unin` folder instead.
* Build the little endian libc: `cd arcgrackle/baselibc ; make ; cd ../..`
	* For Mac99, use the `arcunin/baselibc` folder instead.
* Build the ARC firmware itself: `cd arcgrackle ; make ; cd ..`
	* For Mac99, use the `arcunin` folder instead.

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
