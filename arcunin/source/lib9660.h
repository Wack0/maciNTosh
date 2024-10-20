/* lib9660: a simple ISO9660 reader library especially suited to embedded
 *          systems
 *
 * Copyright © 2014, Owen Shepherd
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice appears in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef LIB9660_H
#define LIB9660_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define L9660_BIG_ENDIAN

#ifdef L9660_HAVE_STDIO
#include <stdio.h>
#define L9660_SEEK_END SEEK_END
#define L9660_SEEK_SET SEEK_SET
#define L9660_SEEK_CUR SEEK_CUR
#else
#define L9660_SEEK_END -1
#define L9660_SEEK_SET  0
#define L9660_SEEK_CUR +1
#endif

#ifdef __INTELLISENSE__
#define L9_PACKED
#else
#define L9_PACKED __attribute__((packed))
#endif

/* Our error return format */
typedef enum {
    /*! Success! */
    L9660_OK = 0,
    /*! read_sector callback returned false */
    L9660_EIO,
    /*! file system is bad */
    L9660_EBADFS,
    /*! specified name does not exist */
    L9660_ENOENT,
    /*! attempted to open a non-file (e.g. a directory) as a file */
    L9660_ENOTFILE,
    /*! attempted to open a non-directory (e.g. a file) as a directory
     *  may be returned by l9660_openat if e.g. you pass path "a/b" and
     *  "a" is a file
     */
    L9660_ENOTDIR,
} l9660_status;

/* ISO9660 uses big/little/dual endian integers */
typedef struct L9_PACKED { uint8_t le[2]; }          l9660_luint16;
typedef struct L9_PACKED { uint8_t be[2]; }          l9660_buint16;
typedef struct L9_PACKED { uint8_t le[2], be[2]; }   l9660_duint16;
typedef struct L9_PACKED { uint8_t le[4]; }          l9660_luint32;
typedef struct L9_PACKED { uint8_t be[4]; }          l9660_buint32;
typedef struct L9_PACKED { uint8_t le[4], be[4]; }   l9660_duint32;

/* Descriptor time format */
typedef struct L9_PACKED {
    char d[17];
} l9660_desctime;

/* File time format */
typedef struct L9_PACKED {
    char d[7];
} l9660_filetime;

/* Directory entry */
typedef struct L9_PACKED {
    uint8_t         length;
    uint8_t         xattr_length;
    l9660_duint32   sector;
    l9660_duint32   size;
    l9660_filetime  time;
    uint8_t         flags;
    uint8_t         unit_size;
    uint8_t         gap_size;
    l9660_duint16   vol_seq_number;
    uint8_t         name_len;
    char            name[/*name_len*/];
} l9660_dirent;

/* Volume descriptor header */
typedef struct L9_PACKED {
    uint8_t type;
    char    magic[5];
    uint8_t version;
} l9660_vdesc_header;

/* Primary volume descriptor */
typedef struct L9_PACKED {
    l9660_vdesc_header  hdr;
    char                pad0[1];
    char                system_id[32];
    char                volume_id[32];
    char                pad1[8];
    l9660_duint32       volume_space_size;
    char                pad2[32];
    l9660_duint16       volume_set_size;
    l9660_duint16       volume_seq_number;
    l9660_duint16       logical_block_size;
    l9660_duint32       path_table_size;
    l9660_luint32       path_table_le;
    l9660_luint32       path_table_opt_le;
    l9660_buint32       path_table_be;
    l9660_buint32       path_table_opt_be;
    union {
        l9660_dirent    root_dir_ent;
        char            pad3[34];
    };
    char                volume_set_id[128];
    char                data_preparer_id[128];
    char                app_id[128];
    char                copyright_file[38];
    char                abstract_file[36];
    char                bibliography_file[37];
    l9660_desctime      volume_created,
                        volume_modified,
                        volume_expires,
                        volume_effective;
    uint8_t             file_structure_version;
    char                pad4[1];
    char                app_reserved[512];
    char                reserved[653];
} l9660_vdesc_primary;

/* A generic volume descriptor (i.e. 2048 bytes) */
typedef union {
    l9660_vdesc_header  hdr;
    char                _bits[2048];
} l9660_vdesc;

/* File system structure.
 * Stick this inside your own structure and cast/offset as appropriate to store
 * private data
 */
typedef struct l9660_fs {
#ifdef L9660_SINGLEBUFFER
    union {
        l9660_dirent    root_dir_ent;
        char            root_dir_pad[34];
    };
#else
    /* Sector buffer to hold the PVD */
    l9660_vdesc pvd;
#endif

    /* read_sector func */
    bool (*read_sector)(struct l9660_fs *fs, void *buf, uint32_t sector);
} l9660_fs;

typedef struct {
#ifndef L9660_SINGLEBUFFER
    /* single sector buffer */
    char buf[2048];
#endif
    l9660_fs *fs;
    uint32_t first_sector;
    uint32_t position;
    uint32_t length;
} l9660_file;

typedef struct {
    /* directories are mostly just files with special accessors, but we like type safetey */
    l9660_file file;
} l9660_dir;

/* Open a file system, initialising *fs. */
l9660_status l9660_openfs(
    l9660_fs *fs,
    bool (*read_sector)(l9660_fs *fs, void *buf, uint32_t sector));

/*void l9660_closefs(l9660_fs *fs); (nop) */

/*! Open the root directory */
l9660_status l9660_fs_open_root(l9660_dir *dir, l9660_fs *fs);

/*! Open the subdirectory given by \p path */
l9660_status l9660_opendirat(l9660_dir *dir, l9660_dir *parent, const char *path);
/*! Returns the next directory entry. If end-of-directory is reached, *dirent is
 *  set to NULL. */
l9660_status l9660_readdir(l9660_dir *dir, l9660_dirent **dirent);

#define l9660_seekdir(dir, pos) (l9660_seek(&(dir)->file, L9660_SEEK_SET, (pos)))
#define l9660_telldir(dir)      (l9660_tell(&(dir)->file))

/*! Open the file given by \p path in \p parent */
l9660_status l9660_openat(l9660_file *file, l9660_dir *parent, const char *path);

/*! Read \p size bytes into \p buf. The number of bytes read will be returned in
 *  \p *read. May be less than \p size (but only 0 on EOF)
 */
l9660_status l9660_read(l9660_file *file, void* buf, size_t size, size_t *read);
/*! Seek the file to \p offset from \p whence */
l9660_status l9660_seek(l9660_file *file, int whence, int32_t offset);
/*! Return the current position (suitable for passing to l9660_seek(file, SEEK_SET, ...)) */
uint32_t     l9660_tell(l9660_file *file);

#endif
