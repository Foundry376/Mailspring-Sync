/*
 * libEtPan! -- a mail stuff library
 *
 * Copyright (C) 2001, 2014 - DINH Viet Hoa
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
 * 3. Neither the name of the libEtPan! project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "mailsasl.h"

#ifdef USE_SASL

#ifdef LIBETPAN_REENTRANT
#if defined(HAVE_PTHREAD_H) && !defined(IGNORE_PTHREAD_H)
#include <pthread.h>
#elif (defined WIN32)
#include <windows.h>
#endif
#endif

#include <sasl/sasl.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if defined(WIN32) && !defined(LIBETPAN_REENTRANT)
// Need windows.h for GetModuleFileNameW even in non-reentrant builds
#include <windows.h>
#endif

#ifdef LIBETPAN_REENTRANT
#if defined(HAVE_PTHREAD_H) && !defined(IGNORE_PTHREAD_H)
static pthread_mutex_t sasl_lock = PTHREAD_MUTEX_INITIALIZER;
#elif (defined WIN32)
static CRITICAL_SECTION sasl_lock = { 0 };
static int sasl_lock_init_done =  0;
#endif
#endif

#ifdef LIBETPAN_REENTRANT
#if defined(HAVE_PTHREAD_H) && !defined(IGNORE_PTHREAD_H)
#define LOCK_SASL() pthread_mutex_lock(&sasl_lock)
#define UNLOCK_SASL() pthread_mutex_unlock(&sasl_lock)
#elif (defined WIN32)
#define LOCK_SASL() EnterCriticalSection(&sasl_lock)
#define UNLOCK_SASL() LeaveCriticalSection(&sasl_lock)
#endif
#else
#define LOCK_SASL() do {} while(0)
#define UNLOCK_SASL() do {} while(0)
#endif

static int sasl_use_count = 0;
static int sasl_init_error = 0;

#ifdef LIBETPAN_REENTRANT

void mailsasl_init_lock(){
#if defined(HAVE_PTHREAD_H) && !defined(IGNORE_PTHREAD_H)
	// nothing to do
#elif (defined WIN32)
  static int volatile mainsasl_init_lock_done = 0;
  if (InterlockedExchange(&mainsasl_init_lock_done, 1) == 0){
    InitializeCriticalSection(&sasl_lock);
  }
#endif
}

void mailsasl_uninit_lock(){
#if defined(HAVE_PTHREAD_H) && !defined(IGNORE_PTHREAD_H)
	// nothing to do
#elif (defined WIN32)
	static int volatile mainsasl_init_lock_done = 0;
	if (InterlockedExchange(&mainsasl_init_lock_done, 1) == 0){
		DeleteCriticalSection(&sasl_lock);
	}
#endif
}


#endif

void mailsasl_external_ref(void)
{
  LOCK_SASL();
  sasl_use_count ++;
  UNLOCK_SASL();
}

void mailsasl_ref(void)
{
  LOCK_SASL();
  sasl_use_count ++;
  if (sasl_use_count == 1) {
#ifdef WIN32
    // On Windows, we must call sasl_set_path() BEFORE sasl_client_init()
    // for SASL to find plugin DLLs.
    //
    // If SASL_PATH is set in the environment, use it (allows user override).
    // Otherwise, default to the executable's directory where plugins are deployed.
    //
    // cyrus-sasl is built with UNICODE (vcpkg-overlay-ports/cyrus-sasl/portfile.cmake) and
    // decodes the plugin path as UTF-8 (lib/windlopen.c), so pass UTF-8, not the ANSI code
    // page: with a non-ASCII folder in the path no plugin loads and AUTH fails with
    // SASL_NOMECH (296).
    wchar_t plugin_dir[MAX_PATH];
    int have_dir = 0;
    const wchar_t *sasl_path = _wgetenv(L"SASL_PATH");
    if (sasl_path != NULL && sasl_path[0] != L'\0') {
      if (wcslen(sasl_path) < MAX_PATH) {
        wcscpy_s(plugin_dir, MAX_PATH, sasl_path);
        have_dir = 1;
      }
    } else {
      DWORD len = GetModuleFileNameW(NULL, plugin_dir, MAX_PATH);
      if (len > 0 && len < MAX_PATH) {
        wchar_t *last_slash = wcsrchr(plugin_dir, L'\\');
        if (last_slash != NULL) {
          *last_slash = L'\0';
          have_dir = 1;
        }
      }
    }
    if (have_dir) {
      char plugin_dir_utf8[MAX_PATH * 4];
      if (WideCharToMultiByte(CP_UTF8, 0, plugin_dir, -1, plugin_dir_utf8, sizeof(plugin_dir_utf8), NULL, NULL) > 0) {
        sasl_set_path(SASL_PATH_TYPE_PLUGIN, plugin_dir_utf8);
      }
    }
#endif
    sasl_init_error = sasl_client_init(NULL);
  }
  UNLOCK_SASL();
}

void mailsasl_unref(void)
{
  LOCK_SASL();
  sasl_use_count --;
  if (sasl_use_count == 0) {
#if 0 /* workaround libsasl bug */
    sasl_done();
#endif
  }
  UNLOCK_SASL();
}

int mailsasl_init_error(void)
{
  return sasl_init_error;
}

#else

void mailsasl_external_ref(void)
{
}

void mailsasl_ref(void)
{
}

void mailsasl_unref(void)
{
}

#endif
