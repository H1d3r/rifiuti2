/* vim: set sw=4 ts=4 noexpandtab : */
/*
 * Copyright (C) 2007-2019 Abel Cheung.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
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
#include <time.h>
#include <stdio.h>
#include <glib.h>

/* Error and exit status */
typedef enum
{
	R2_OK = 0, /* as synonym of EXIT_SUCCESS */
	R2_ERR_ARG,
	R2_ERR_OPEN_FILE,
	R2_ERR_BROKEN_FILE,  /* file format validation failure */
	R2_ERR_WRITE_FILE,
	R2_ERR_USER_ENCODING,
	R2_ERR_INTERNAL = 64
} r2status;

typedef enum
{
	RECYCLE_BIN_TYPE_FILE,
	RECYCLE_BIN_TYPE_DIR,
} rbin_type;

/* The first 4 or 8 bytes of recycle bin index files */
enum
{
	/* negative number means error when retrieving version info */
	VERSION_INCONSISTENT = -2,
	VERSION_NOT_FOUND,

	/* $Recycle.bin */
	VERSION_VISTA = 1,
	VERSION_WIN10,

	/* INFO / INFO2 */
	VERSION_WIN95 = 0,
	VERSION_NT4   = 2,
	VERSION_WIN98 = 4,
	VERSION_ME_03,
};

/*
 * The following enum is different from the versions above.
 * This is more detailed breakdown, and for detection of exact
 * Windows version from various recycle bin artifacts.
 * WARNING: MUST match os_strings string array
 */
typedef enum
{
	OS_GUESS_UNKNOWN = -1,
	OS_GUESS_95,
	OS_GUESS_NT4,
	OS_GUESS_98,
	OS_GUESS_ME,
	OS_GUESS_2K,
	OS_GUESS_XP_03,
	OS_GUESS_2K_03,   /* Empty recycle bin, full detection impossible */
	OS_GUESS_VISTA,   /* includes everything up to 8.1 */
	OS_GUESS_10
} _os_guess;

enum
{
	OUTPUT_NONE,
	OUTPUT_CSV,
	OUTPUT_XML
};

/*! \struct _rbin_meta
 *  \brief Metadata for recycle bin
 */
typedef struct _rbin_meta
{
	rbin_type       type;
	const char     *filename;
	int64_t         version;
	uint32_t        recordsize;          /*!< INFO2 only */
	uint32_t        total_entry;         /*!< 95/NT4 only */
	gboolean        keep_deleted_entry;  /*!< 98-03 only, add extra output column */
	gboolean        is_empty;
	gboolean        has_unicode_path;
	gboolean        fill_junk;  /*!< TRUE for 98/ME/2000 only, some fields padded
	                                 with junk data instaed of zeroed */
} metarecord;

/*! \struct _rbin_struct
 *  \brief Struct for single recycle bin item
 */
typedef struct _rbin_struct
{
	/*! For $Recycle.bin, version of each index file is kept here,
	 * while meta.version keeps the global status of whole dir */
	uint64_t          version;          /* $Recycle.bin only */

	/*! Each record links to metadata for more convenient access */
	const metarecord *meta;

	/*! \brief Number is for INFO2, file name for $Recycle.bin */
	union
	{
		uint32_t      index_n;          /* INFO2 only */
		char         *index_s;          /* $Recycle.bin only */
	};

	/*! Item delection time */
	GDateTime        *deltime;
	int64_t           winfiletime;     /* for internal sorting */

	/*! Can mean cluster size or actual file/folder size */
	uint64_t          filesize;

	/* despite var names, all filenames are converted to UTF-8 upon parsing */
	char             *uni_path;
	char             *legacy_path;     /* INFO2 only */

	gboolean          emptied;         /* INFO2 only */
	unsigned char     drive;           /* INFO2 only */
} rbin_struct;

/* convenience macro */
#define copy_field(field, off1, off2) memcpy((field), \
		buf + off1 ## _OFFSET, off2 ## _OFFSET - off1 ## _OFFSET)

/*! Every Windows use this GUID in recycle bin desktop.ini */
#define RECYCLE_BIN_CLSID "645FF040-5081-101B-9F08-00AA002F954E"

/*
 * Most versions of recycle bin use full PATH_MAX (260 char) to store file paths,
 * in either ANSI or Unicode variations, except Windows 10 which uses variable size.
 * However we don't want to use PATH_MAX directly since on Linux/Unix it's
 * another thing.
 */
#define WIN_PATH_MAX 260

/* shared functions */
void          rifiuti_init                (const char       *progpath);

void          rifiuti_setup_opt_ctx       (GOptionContext    **context,
                                           rbin_type         type);

r2status      rifiuti_parse_opt_ctx       (GOptionContext  **context,
                                           int              *argc,
                                           char           ***argv);

GDateTime *   win_filetime_to_gdatetime   (int64_t           win_filetime);

char *        utf16le_to_utf8             (const gunichar2  *str,
                                           glong             len,
                                           glong            *items_read,
                                           glong            *items_written,
                                           GError          **error)
                                           G_GNUC_UNUSED;

int           check_file_args             (const char       *path,
                                           GSList          **list,
                                           rbin_type         type);

r2status      prepare_output_handle       (void);

void          close_output_handle         (void);
void          close_error_handle          (void);

void          print_header                (metarecord        meta);

void          print_record_cb             (rbin_struct      *record);

void          print_footer                (void);

r2status      move_temp_file              (void);

void          print_version_and_exit      (void) G_GNUC_NORETURN;

void          free_record_cb              (rbin_struct      *record);

char *        conv_path_to_utf8_with_tmpl (const char      *str,
                                           const char      *from_enc,
                                           const char      *tmpl,
                                           size_t          *read,
                                           r2status        *st);

void          free_vars                   (void);

#endif
