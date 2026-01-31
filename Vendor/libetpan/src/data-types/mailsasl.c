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

#if defined(WIN32) && !defined(LIBETPAN_REENTRANT)
// Need windows.h for GetModuleFileNameA even in non-reentrant builds
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
    char *sasl_path = getenv("SASL_PATH");
    if (sasl_path != NULL && sasl_path[0] != '\0') {
      // User-specified path takes precedence
      sasl_set_path(SASL_PATH_TYPE_PLUGIN, sasl_path);
    } else {
      // Default to executable directory
      char exe_path[MAX_PATH];
      DWORD len = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
      if (len > 0 && len < MAX_PATH) {
        // Strip filename to get directory
        char *last_slash = strrchr(exe_path, '\\');
        if (last_slash != NULL) {
          *last_slash = '\0';
          sasl_set_path(SASL_PATH_TYPE_PLUGIN, exe_path);
        }
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
