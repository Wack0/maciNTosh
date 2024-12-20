## OldWorldIsoBuilder
This tool will take a standard ISO file created by your favourite tool and inject the PM partition table, drivers and HFS partition image to make it bootable on an Old World system (assuming the HFS partition is itself bootable).

You need the driver files for this tool to work, you can extract them from a Mac OS X install media (path is something like `/System/Library/PrivateFrameworks/MediaKit.framework/Versions/A/Resources/MKDrivers.bundle/Contents/Resources`). The files you need are:

- `Apple_Driver43.ptDR.drvr` (SCSI patch driver)
- `Apple_Driver43_CD.CDrv.drvr` (SCSI CD driver)
- `Apple_Driver_ATAPI.ptDR.drvr` (ATAPI patch driver)
- `Apple_Driver_ATAPI.ATPI.drvr` (ATAPI main driver)

The tool will also work with raw images of these partitions. 

Additionally this tool needs the raw image of the CD Apple Patch partition (containing patches `mesh / Mesh Itt Patch`, `scsi / CD SCSIMgr` and `snag / CDSnag`), you can extract this from a Mac OS X install media using 7-Zip (v24.09 or higher, this version fixes some bugs in APM parsing), example command line: `7z x -tapm "Apple Mac OS X 10.3.0 - Disk 1.iso" "Patch Partition.Apple_Patches"`

Command line for this tool is as follows:
`oldiso <iso> <hfs_partition.img> Apple_Driver43.ptDR.drvr Apple_Driver43_CD.CDrv.drvr Apple_Driver_ATAPI.ptDR.drvr Apple_Driver_ATAPI.ATPI.drvr Patch Partition.Apple_Patches`

The iso file provided will be overwritten on disk, so make a backup of the original first.

Build `oldiso.c` with gcc: `gcc -ooldiso oldiso.c` or `x86_64-w64-mingw32-gcc -ooldiso.exe oldiso.c` (etc). **clang does not work** due to not currently supporting `scalar_storage_order`.
