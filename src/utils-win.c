/* vim: set sw=4 ts=4 noexpandtab : */
/*
 * Copyright (C) 2015-2019 Abel Cheung.
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

#include "config.h"
#include "utils-win.h"

#include <lmcons.h>
#include <windows.h>
#include <aclapi.h>
#include <authz.h>

#include <glib.h>
#include <glib/gi18n.h>


HANDLE  wincon_fh = NULL;


/* GUI message box */
void
gui_message (const char *message)
{
	gunichar2 *title, *body;
	GError *error = NULL;

	title = g_utf8_to_utf16 (_("This is a command line application"),
		-1, NULL, NULL, &error);
	if (error) {
		g_clear_error (&error);
		title = g_utf8_to_utf16 ("This is a command line application",
			-1, NULL, NULL, NULL);
	}

	body = g_utf8_to_utf16 (message, -1, NULL, NULL, &error);
	if (error) {
		g_clear_error (&error);
		body = g_utf8_to_utf16 ("(Original message failed to be displayed in UTF-16)",
			-1, NULL, NULL, NULL);
	}

	/* Takes advantage of the fact that LPCWSTR (wchar_t) is actually 16bit on Windows */
	MessageBoxW (NULL, (LPCWSTR) body, (LPCWSTR) title,
		MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
	g_free (title);
	g_free (body);
}

/*!
 * A copy of latter part of g_win32_getlocale()
 */
#ifndef SUBLANG_SERBIAN_LATIN_BA
#define SUBLANG_SERBIAN_LATIN_BA 0x06
#endif

static const char *
get_win32_locale_script (int primary,
                         int sub)
{
	switch (primary)
	{
	  case LANG_AZERI:
		switch (sub)
		{
		  case SUBLANG_AZERI_LATIN:    return "@Latn";
		  case SUBLANG_AZERI_CYRILLIC: return "@Cyrl";
		}
		break;

	  case LANG_SERBIAN:		/* LANG_CROATIAN == LANG_SERBIAN */
		switch (sub)
		{
		  case SUBLANG_SERBIAN_LATIN:
		  case SUBLANG_SERBIAN_LATIN_BA: /* Serbian (Latin) - Bosnia and Herzegovina */
			return "@Latn";
		}
		break;
	  case LANG_UZBEK:
		switch (sub)
		{
		  case SUBLANG_UZBEK_LATIN:    return "@Latn";
		  case SUBLANG_UZBEK_CYRILLIC: return "@Cyrl";
		}
		break;
	}
	return NULL;
}

/*!
 * We can't use [`g_win32_getlocale()`][1] directly.
 *
 * There are 4 possible source for language UI settings:
 * - GetThreadLocale()  (used by g_win32_getlocale)
 * - Installed language group
 * - User default language
 * - System default language
 *
 * First one is no good because rifiuti2 is a CLI program, where
 * the caller is a console, so this 'language' is merely determined
 * by console code page, which is not always equivalent to user
 * preferred language. For example, CP850 can be used by multiple
 * european languages but GetLocaleInfo() always treat it as en_US.
 * Language group is not an indicator too because it can imply
 * several similar languages.
 *
 * Here we attempt to use User default first, followed by GetThreadLocale
 * locale; and do the dirty work in a manner almost identical to
 * g_win32_getlocale().
 *
 * [1]: https://developer.gnome.org/glib/stable/glib-Windows-Compatibility-Functions.html#g-win32-getlocale
 */
char *
get_win32_locale (void)
{
	LCID lcid;
	LANGID langid;
	char *ev;
	char iso639[10];
	char iso3166[10];
	const char *script;

	/* Allow user overriding locale env */
	if (((ev = getenv ("LC_ALL"))      != NULL && ev[0] != '\0') ||
	    ((ev = getenv ("LC_MESSAGES")) != NULL && ev[0] != '\0') ||
	    ((ev = getenv ("LANG"))        != NULL && ev[0] != '\0'))
	return g_strdup (ev);

	lcid = LOCALE_USER_DEFAULT;
	if (!GetLocaleInfo (lcid, LOCALE_SISO639LANGNAME , iso639 , sizeof (iso639)) ||
	    !GetLocaleInfo (lcid, LOCALE_SISO3166CTRYNAME, iso3166, sizeof (iso3166)))
	{
		lcid = GetThreadLocale();
		if (!GetLocaleInfo (lcid, LOCALE_SISO639LANGNAME , iso639 , sizeof (iso639)) ||
			!GetLocaleInfo (lcid, LOCALE_SISO3166CTRYNAME, iso3166, sizeof (iso3166)))
		return g_strdup ("C");
	}

	/* Strip off the sorting rules, keep only the language part.  */
	langid = LANGIDFROMLCID (lcid);

	/* Get script based on language and territory */
	script = get_win32_locale_script (PRIMARYLANGID (langid), SUBLANGID (langid));

	return g_strconcat (iso639, "_", iso3166, script, NULL);
}

/*!
 * Retrieve current user name and convert it to SID
 *
 * Following functions originates from [example of `GetEffectiveRightsFromAcl()`][1],
 * which is not about the function itself but a _replacement_ of it (shrug).
 *
 * [1]: https://msdn.microsoft.com/en-us/library/windows/desktop/aa446637(v=vs.85).aspx
 */
static PSID
get_user_sid (void)
{
	gboolean       status;
	char           username[UNLEN + 1], *errmsg;
	DWORD          err = 0, bufsize = UNLEN + 1, sidsize = 0, domainsize = 0;
	PSID           sid;
	LPTSTR         domainname;
	SID_NAME_USE   sidtype;

	if ( !GetUserName (username, &bufsize) )
	{
		errmsg = g_win32_error_message (GetLastError());
		g_critical (_("Failed to get current user name: %s"), errmsg);
		goto getsid_fail;
	}

	status = LookupAccountName (NULL, username, NULL, &sidsize,
			NULL, &domainsize, &sidtype);
	if ( !status )
		err = GetLastError();
	g_debug ("1st LookupAccountName(): status = %d", (int) status);

	if ( err != ERROR_INSUFFICIENT_BUFFER )
	{
		errmsg = g_win32_error_message (err);
		g_critical (_("LookupAccountName() failed: %s"), errmsg);
		goto getsid_fail;
	}

	sid = (PSID) g_malloc (sidsize);
	domainname = (LPTSTR) g_malloc (domainsize);

	status = LookupAccountName (NULL, username, sid, &sidsize,
			domainname, &domainsize, &sidtype);
	err = status ? 0 : GetLastError();
	g_debug ("2nd LookupAccountName(): status = %d", (int) status);
	g_free (domainname);  /* unused */

	if ( status != 0 )
		return sid;    /* success */

	errmsg = g_win32_error_message (err);
	g_critical (_("LookupAccountName() failed: %s"), errmsg);
	g_free (sid);

  getsid_fail:
	g_free (errmsg);
	return NULL;
}

/*!
 * Fetch ACL access mask using Authz API
 */
gboolean
can_list_win32_folder (const char *path)
{
	char                  *errmsg = NULL;
	gunichar2             *wpath;
	gboolean               ret = FALSE;
	PSID                   sid;
	DWORD                  dw, dw2;
	PSECURITY_DESCRIPTOR   sec_desc;
	ACCESS_MASK            mask;
	AUTHZ_RESOURCE_MANAGER_HANDLE authz_manager;
	AUTHZ_CLIENT_CONTEXT_HANDLE   authz_ctxt = NULL;
	AUTHZ_ACCESS_REQUEST          authz_req = { MAXIMUM_ALLOWED, NULL, NULL, 0, NULL };
	AUTHZ_ACCESS_REPLY            authz_reply;

	if ( NULL == ( sid = get_user_sid() ) )
		return FALSE;

	wpath = g_utf8_to_utf16 (path, -1, NULL, NULL, NULL);
	if (wpath == NULL)
		return FALSE;

	if ( !AuthzInitializeResourceManager (AUTHZ_RM_FLAG_NO_AUDIT,
				NULL, NULL, NULL, NULL, &authz_manager) )
	{
		errmsg = g_win32_error_message (GetLastError());
		g_printerr (_("AuthzInitializeResourceManager() failed: %s"), errmsg);
		g_printerr ("\n");
		goto traverse_fail;
	}

	dw = GetNamedSecurityInfoW ((wchar_t *)wpath, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION |
			OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
			NULL, NULL, NULL, NULL, &sec_desc);
	if ( dw != ERROR_SUCCESS )
	{
		errmsg = g_win32_error_message (dw);
		g_printerr (_("Failed to retrieve Discretionary ACL info for '%s': %s"), path, errmsg);
		g_printerr ("\n");
		goto traverse_getacl_fail;
	}

	if ( !AuthzInitializeContextFromSid (0, sid, authz_manager,
				NULL, (LUID) {0} /* unused */, NULL, &authz_ctxt) )
	{
		errmsg = g_win32_error_message (GetLastError());
		g_printerr (_("AuthzInitializeContextFromSid() failed: %s"), errmsg);
		g_printerr ("\n");
		goto traverse_getacl_fail;
	}

	authz_reply = (AUTHZ_ACCESS_REPLY) { 1, &mask, &dw2, &dw }; /* last 2 param unused */
	if ( !AuthzAccessCheck (0, authz_ctxt, &authz_req, NULL, sec_desc,
				NULL, 0, &authz_reply, NULL ) )
	{
		errmsg = g_win32_error_message (GetLastError());
		g_printerr (_("AuthzAccessCheck() failed: %s"), errmsg);
		g_printerr ("\n");
	}
	else
	{
		/*
		 * We only need permission to list directory; even directory traversal is
		 * not needed, because we are going to access the files directly later.
		 * Unlike Unix, no read permission on parent folder is needed to list
		 * files within.
		 */
		if ( (mask & FILE_LIST_DIRECTORY) == FILE_LIST_DIRECTORY &&
				(mask & FILE_READ_EA) == FILE_READ_EA )
			ret = TRUE;
		else {
			g_printerr (_("Error listing directory: Insufficient permission."));
			g_printerr ("\n");
		}

		/* use glib type to avoid including more header */
		g_debug ("Access Mask hex for '%s': 0x%X", path, (guint32) mask);
	}

	AuthzFreeContext (authz_ctxt);

  traverse_getacl_fail:
	LocalFree (sec_desc);
	AuthzFreeResourceManager (authz_manager);

  traverse_fail:
	g_free (sid);
	g_free (errmsg);
	g_free (wpath);
	return ret;
}

/*!
 * Initialize console handle under Windows
 *
 * Used only when output is Windows native console. For all other cases
 * unix-style file stream is used.
 */
gboolean
init_wincon_handle (void)
{
	HANDLE h = GetStdHandle (STD_OUTPUT_HANDLE);

	/*
	 * FILE_TYPE_CHAR only happens when output is a native Windows cmd
	 * console. For Cygwin and Msys shell environments (and output redirection),
	 * GetFileType() would return FILE_TYPE_PIPE. In those cases printf
	 * family outputs UTF-8 data properly. Only Windows console needs to be
	 * dealt with using wide char API.
	 */
	if (GetFileType (h) == FILE_TYPE_CHAR) {
		wincon_fh = h;
		return TRUE;
	}
	return FALSE;
}

void
close_wincon_handle (void)
{
	if (wincon_fh != NULL)
		CloseHandle (wincon_fh);
	return;
}

gboolean
puts_wincon (const wchar_t *wstr)
{
	g_return_val_if_fail (wstr      != NULL, FALSE);
	g_return_val_if_fail (wincon_fh != NULL, FALSE);

	if (WriteConsoleW (wincon_fh, wstr, wcslen (wstr), NULL, NULL))
		return TRUE;
	else
		return FALSE;
}
