/*
 * Copyright (C) 2007-2023, Abel Cheung.
 * rifiuti2 is released under Revised BSD License.
 * Please see LICENSE file for more info.
 */

#ifndef _RIFIUTI_UTILS_H
#define _RIFIUTI_UTILS_H

/*
 * Rifiuti itself only need _POSIX_C_SOURCE == 1 for usage of
 * localtime_r(); however glib2's usage of siginfo_t pushes
 * the requirement further. It's undefined in some Unices.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <inttypes.h>
#include <stdio.h>
#include <glib.h>

// https://stackoverflow.com/a/3599170
#define UNUSED(x) (void)(x)

/* exit status */
typedef enum
{
    EXIT_OK = EXIT_SUCCESS,
    EXIT_ERR_ARG,  /* Command argument parsing error */
    EXIT_ERR_OPEN_FILE,  /* Fail when searching and opening index file */
    EXIT_ERR_WRITE_FILE,  /* Error writing output, includes manipulation of temp file */
    EXIT_ERR_ILLEGAL_DATA,  /* serious file format validation failure */
    EXIT_ERR_DUBIOUS_DATA,  /* affect some record(s) only */
    EXIT_ERR_NO_LIVE,  // live mode requested but failed
    EXIT_ERR_UNHANDLED = 64,
} exitcode;

typedef enum
{
    RECYCLE_BIN_TYPE_UNKNOWN = 0,
    RECYCLE_BIN_TYPE_FILE,
    RECYCLE_BIN_TYPE_DIR,
} rbin_type;

/* The first 4 or 8 bytes of recycle bin index files */
typedef enum
{
    /* negative number = error */

    VERSION_INCONSISTENT = -2,  /* Mixed versions in same folder */
    VERSION_NOT_FOUND,  /* Empty $Recycle.bin */

    /* $Recycle.bin */

    VERSION_VISTA = 1,
    VERSION_WIN10,

    /* INFO / INFO2 */

    VERSION_WIN95 = 0,
    VERSION_NT4   = 2,
    VERSION_WIN98 = 4,
    VERSION_ME_03,
} detected_os_ver;

typedef enum _out_fmt
{
    FORMAT_UNKNOWN,
    FORMAT_TEXT,
    FORMAT_XML,
    FORMAT_JSON,
} out_fmt;

/**
 * @brief Whether original trashed file still exists
 */
typedef enum
{
    FILESTATUS_UNKNOWN = 0,
    FILESTATUS_EXISTS,
    FILESTATUS_GONE
} trash_file_status;

/**
 * @brief Metadata for recycle bin
 * @note This is a merge of `INFO2` and `$Recycle.bin` elements.
 */
typedef struct _rbin_meta
{
    rbin_type type;  /* `INFO2` or `$Recycle.bin` format */
    char *filename;  /* File or dir name of trash can itself */
    /**
     * @brief The global recycle bin version
     * @note For `INFO2`, the value is stored in certain bytes of `INFO2` index file.
     * For `$Recycle.bin`, it is determined collectively from all index files within
     * the folder.
     */
    int64_t version;
    /**
     * @brief Size of each trash record within index file
     * @note It is either 280 or 800 bytes, depending on Windows version
     * @attention For `INFO2` only. `$Recycle.bin` has only one record per file.
     */
    gsize recordsize;
    /**
     * @brief Total entry ever existed in `INFO2` file
     * @note On Windows 95 and NT 4.x, `INFO2` keeps a field for counting number
     * of trashed entries. The field is unused afterwards.
     * @attention For `INFO2` only
     */
    uint32_t total_entry;
    /**
     * @brief Whether empty spaces in index file was padded with junk data
     * @note For Windows 98, ME and 2000, paths and fields are not padded with
     * zero filled memory, but with arbitrary random data, presumably memory
     * segments due to sloppy programming practice.
     * @attention For `INFO2` only
     */
    gboolean fill_junk;
    /**
     * @brief List of trash file records pointer
     */
    GPtrArray *records;
    /**
     * @brief List of invalid records and their errors
     */
    GHashTable *invalid_records;

} metarecord;

/**
 * @brief Structure for single recycle bin item
 * @note This is a merge of `INFO2` and `$Recycle.bin` elements.
 */
typedef struct _rbin_struct
{
    /**
     * @brief version of each index file
     * @note `meta.version` keeps the global status of whole dir,
     * while this one keeps individual version of index file.
     * @attention For `$Recycle.bin` only
     */
    uint64_t version;
    /**
     * @brief Chronological index number for INFO2
     * @attention For `INFO2` only
     */
    uint32_t index_n;

    /**
     * @brief Index file name
     * @attention For `$Recyle.bin` only
     */
    char *index_s;

    GDateTime *deltime;  /* Item trashing time */

    /**
     * @brief Trashed time (`deltime`) stored as Windows datetime integer
     * @note For internal entry sorting in `$Recycle.bin`. `INFO2` records sort using `index_n` field.
     */
    int64_t winfiletime;

    /**
     * @brief Trashed file size
     * @note Can mean cluster size or actual file/folder size,
     * depending on Recycle bin version. Not invertigated
     * thoroughly yet.
     */
    uint64_t filesize;

    /* despite var names, all filenames are converted to UTF-8 upon parsing */

    /**
     * @brief Unicode trashed file original path
     * @note Original path was stored in index file in UTF-16 encoding
     * since Windows 2000. The path is converted to UTF-8 encoding and stored here .
     */
    char *raw_uni_path;

    /**
     * @brief ANSI encoded trash file original path
     * @note Until Windows 2003, index file preserves trashed file path in
     * ANSI code page. The path is converted to UTF-8 encoding and stored here.
     * @attention For `INFO2` only. Can be either full path or using 8.3 format,
     * depending on Windows version and code page used.
     */
    char *raw_legacy_path;

    /**
     * @brief Whether original trashed file is gone
     * @note Trash file can be detected if it still exists, but via very
     * different mechanisms on different formats. For `INFO2`, one can
     * only deduce if it is either permanently removed, or restored on
     * filesystem. For `$Recycle.bin`, it is guaranteed to be restored
     * if `$R...` named trashed file doesn't exist in folder.
     */
    trash_file_status gone;
    /**
     * @brief Drive letter for removed trash entry
     * @note If `INFO2` entry is marked as gone, first letter of original
     * path is removed and stored elsewhere, which corresponds to drive letter.
     * @attention For `INFO2` only
     */
    unsigned char drive;
    /**
     * @brief Error associated with this trash entry
     */
    GError *error;

} rbin_struct;

/* convenience macro */
#define copy_field(field, off1, off2) \
    memcpy((field), buf + (off1), (off2) - (off1))

/*! Every Windows use this GUID in recycle bin desktop.ini */
#define RECYCLE_BIN_CLSID "645FF040-5081-101B-9F08-00AA002F954E"

/*
 * Most versions of recycle bin use full PATH_MAX (260 char) to store file paths,
 * in either ANSI or Unicode variations, except Windows 10 which uses variable size.
 * However we don't want to use PATH_MAX directly since on Linux/Unix it's
 * another thing.
 */
#define WIN_PATH_MAX 260


typedef void (*ParseIdxFunc)              (const char       *path,
                                           metarecord       *meta);

/* shared functions */
gboolean      rifiuti_init                (rbin_type         type,
                                           char             *usage_param,
                                           char             *usage_summary,
                                           char           ***argv,
                                           GError          **error);

GDateTime *   win_filetime_to_gdatetime   (int64_t           win_filetime);

gboolean      dump_content                (GError          **error);

exitcode      rifiuti_cleanup             (GError          **error);

void          hexdump                     (void             *start,
                                           size_t            size);

void          do_parse_records            (ParseIdxFunc      func);

#endif
