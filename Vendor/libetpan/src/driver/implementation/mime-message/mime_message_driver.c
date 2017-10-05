/*
 * libEtPan! -- a mail stuff library
 *
 * Copyright (C) 2001, 2005 - DINH Viet Hoa
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

/*
 * $Id: mime_message_driver.c,v 1.13 2008/02/17 13:13:27 hoa Exp $
 */

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "mime_message_driver.h"

#include "libetpan-config.h"

#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#	include <sys/mman.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "mailmessage.h"
#include "mailmessage_tools.h"
#include "maildriver_tools.h"

int mime_message_set_tmpdir(mailmessage * msg, char * tmpdir)
{
  /* not used now */
  return MAIL_NO_ERROR;
}

void mime_message_detach_mime(mailmessage * msg)
{
  msg->msg_mime = NULL;
}

mailmessage * mime_message_init(struct mailmime * mime)
{
  mailmessage * msg;
  int r;
  
  msg = mailmessage_new();
  if (msg == NULL)
    goto err;

  r = mailmessage_init(msg, NULL, mime_message_driver, 0, 0);
  if (r != MAIL_NO_ERROR)
    goto free;
  
  if (mime != NULL) {
    mailmime_free(msg->msg_mime);
    msg->msg_mime = mime;
  }
  
  return msg;
  
 free:
  mailmessage_free(msg);
 err:
  return NULL;
}

static int initialize(mailmessage * msg)
{
  struct mailmime * mime;
  int res;
  
  mime = mailmime_new_message_data(NULL);
  if (mime == NULL) {
    res = MAIL_ERROR_MEMORY;
    goto err;
  }
  
  msg->msg_mime = mime;
  
  return MAIL_NO_ERROR;
  
 err:
  return res;
}

static void uninitialize(mailmessage * msg)
{
  /* tmp dir name */
  if (msg->msg_data != NULL)
    free(msg->msg_data);
  
  if (msg->msg_mime != NULL)
    mailmime_free(msg->msg_mime);
  msg->msg_mime = NULL;
}

static void flush(mailmessage * msg)
{
  /* do nothing */
}

static void check(mailmessage * msg)
{
  /* do nothing */
}

static void fetch_result_free(mailmessage * msg_info, char * content)
{
  mmap_string_unref(content);
}

static int body_to_mmapstr(char * data, size_t size,
    char ** result, size_t * result_len)
{
  MMAPString * mmapstr;
  size_t cur_token;
  int res;
  int r;
  
  cur_token = 0;
  
  /* skip header */
  
  while (1) {
    r = mailimf_ignore_field_parse(data, size, &cur_token);
    if (r == MAILIMF_NO_ERROR) {
      /* do nothing */
    }
    else
      break;
  }
  
  r = mailimf_crlf_parse(data, size, &cur_token);
  if ((r != MAILIMF_NO_ERROR) && (r != MAILIMF_ERROR_PARSE)) {
    res = maildriver_imf_error_to_mail_error(r);
    goto err;
  }
  
  mmapstr = mmap_string_new_len(data + cur_token, size - cur_token);
  if (mmapstr == NULL) {
    res = MAIL_ERROR_MEMORY;
    
    goto err;
  }
  
  r = mmap_string_ref(mmapstr);
  if (r != 0) {
    mmap_string_free(mmapstr);
    res = MAIL_ERROR_MEMORY;
    
    goto err;
  }
  
  * result = mmapstr->str;
  * result_len = mmapstr->len;

  return MAIL_NO_ERROR;

 err:
  return res;
}

static int body_body_to_mmapstr(char * data, size_t size,
    char ** result, size_t * result_len)
{
  size_t cur_token;
  int res;
  int r;
  
  cur_token = 0;
  
  /* skip header */
  
  /* MIME header */
  
  while (1) {
    r = mailimf_ignore_field_parse(data, size, &cur_token);
    if (r == MAILIMF_NO_ERROR) {
      /* do nothing */
    }
    else
      break;
  }
  
  r = mailimf_crlf_parse(data, size, &cur_token);
  if ((r != MAILIMF_NO_ERROR) && (r != MAILIMF_ERROR_PARSE)) {
    res = maildriver_imf_error_to_mail_error(r);
    goto err;
  }
  
  return body_to_mmapstr(data + cur_token, size - cur_token,
      result, result_len);
  
 err:
  return res;
}


static int fetch_section(mailmessage * msg_info,
    struct mailmime * mime,
    char ** result, size_t * result_len)
{
  int r;
  int res;
  int col;
  MMAPString * str;
  
  if (msg_info->msg_mime == NULL)
    return MAIL_ERROR_INVAL;
  
  str = mmap_string_new("");
  if (str == NULL) {
    res = MAILIMF_ERROR_MEMORY;
    goto err;
  }
  
  col = 0;
  r = mailmime_write_mem(str, &col, mime);
  if (r != MAILIMF_NO_ERROR) {
    res = maildriver_imf_error_to_mail_error(r);
    goto free;
  }
  
  if (mime->mm_parent == NULL) {
    r = mmap_string_ref(str);
    if (r < 0) {
      res = MAIL_ERROR_MEMORY;
      goto free;
    }
    
    * result = str->str;
    * result_len = str->len;
    
    r = MAIL_NO_ERROR;
  }
  else {
    r = body_to_mmapstr(str->str, str->len, result, result_len);
    if (r == MAIL_NO_ERROR) {
      mmap_string_free(str);
    }
  }
  
  if (r != MAIL_NO_ERROR) {
    res = r;
    goto free;
  }
  
  return MAIL_NO_ERROR;

 free:
  mmap_string_free(str);
 err:
  return res;
}


static int fetch_section_header(mailmessage * msg_info,
    struct mailmime * mime,
    char ** result, size_t * result_len)
{
  int r;
  int res;
  int col;
  MMAPString * str;
  
  if (msg_info->msg_mime == NULL)
    return MAIL_ERROR_INVAL;
  
  str = mmap_string_new("");
  if (str == NULL) {
    res = MAIL_ERROR_MEMORY;
    goto err;
  }
  
  col = 0;
  if (mime->mm_type == MAILMIME_MESSAGE) {
    if (mime->mm_data.mm_message.mm_fields != NULL) {
      r = mailimf_fields_write_mem(str, &col, mime->mm_data.mm_message.mm_fields);
      if (r != MAILIMF_NO_ERROR) {
        res = maildriver_imf_error_to_mail_error(r);
        goto free;
      }
      mailimf_string_write_mem(str, &col, "\r\n", 2);
    }
  }
  
  r = mmap_string_ref(str);
  if (r < 0) {
    res = MAIL_ERROR_MEMORY;
    goto free;
  }
  
  * result = str->str;
  * result_len = str->len;
  
  return MAIL_NO_ERROR;

 free:
  mmap_string_free(str);
 err:
  return res;
}


static int fetch_section_mime(mailmessage * msg_info,
    struct mailmime * mime,
    char ** result, size_t * result_len)
{
  int r;
  int res;
  int col;
  MMAPString * str;

  if (msg_info->msg_mime == NULL)
    return MAIL_ERROR_INVAL;

  str = mmap_string_new("");
  if (str == NULL) {
    res = MAIL_ERROR_MEMORY;
    goto err;
  }
  
  col = 0;
  if (mime->mm_content_type != NULL) {
    r = mailmime_content_write_mem(str, &col, mime->mm_content_type);
    if (r != MAILIMF_NO_ERROR) {
      res = maildriver_imf_error_to_mail_error(r);
      goto free;
    }
  }
  if (mime->mm_mime_fields != NULL) {
    r = mailmime_fields_write_mem(str, &col, mime->mm_mime_fields);
    if (r != MAILIMF_NO_ERROR) {
      res = maildriver_imf_error_to_mail_error(r);
      goto free;
    }
  }
  mailimf_string_write_mem(str, &col, "\r\n", 2);
  
  r = mmap_string_ref(str);
  if (r < 0) {
    res = MAIL_ERROR_MEMORY;
    goto free;
  }

  * result = str->str;
  * result_len = str->len;
  
  return MAIL_NO_ERROR;

 free:
  mmap_string_free(str);
 err:
  return res;
}



static int fetch_section_body(mailmessage * msg_info,
    struct mailmime * mime,
    char ** result, size_t * result_len)
{
  int r;
  int res;
  int col;
  MMAPString * str;
  
  if (msg_info->msg_mime == NULL)
    return MAIL_ERROR_INVAL;

  str = mmap_string_new("");
  if (str == NULL) {
    res = MAIL_ERROR_MEMORY;
    goto err;
  }
  
  col = 0;
  if (mime->mm_mime_fields != NULL) {
    r = mailmime_write_mem(str, &col, mime);
    if (r != MAILIMF_NO_ERROR) {
      res = maildriver_imf_error_to_mail_error(r);
      goto free;
    }
  }
  
  if (mime->mm_type == MAILMIME_MESSAGE)
    r = body_body_to_mmapstr(str->str, str->len, result, result_len);
  else
    r = body_to_mmapstr(str->str, str->len, result, result_len);
  
  if (r != MAIL_NO_ERROR) {
    res = r;
    goto free;
  }
  
  mmap_string_free(str);
  
  return MAIL_NO_ERROR;
  
 free:
  mmap_string_free(str);
 err:
  return res;
}


static int get_bodystructure(mailmessage * msg_info,
    struct mailmime ** result)
{
  if (msg_info->msg_mime == NULL)
    return MAIL_ERROR_INVAL;
  
  * result = msg_info->msg_mime;
  
  return MAIL_NO_ERROR;
}


static int fetch(mailmessage * msg_info,
    char ** result, size_t * result_len)
{
  return fetch_section(msg_info, msg_info->msg_mime, result, result_len);
}

static int fetch_header(mailmessage * msg_info,
    char ** result, size_t * result_len)
{
  return fetch_section_header(msg_info,
      msg_info->msg_mime, result, result_len);
}

static int fetch_body(mailmessage * msg_info,
    char ** result, size_t * result_len)
{
  return fetch_section_body(msg_info, msg_info->msg_mime, result, result_len);
}


static int fetch_size(mailmessage * msg_info,
    size_t * result)
{
  char * msg;
  int r;

  msg = NULL;
  r = fetch(msg_info, &msg, result);
  if (r != MAIL_NO_ERROR) {
    return r;
  }
  
  fetch_result_free(msg_info, msg);

  return MAIL_NO_ERROR;
}


static mailmessage_driver local_mime_message_driver = {
  /* msg_name */ "mime",
  
  /* msg_initialize */ initialize,
  /* msg_uninitialize */ uninitialize,
  
  /* msg_flush */ flush,
  /* msg_check */ check,

  /* msg_fetch_result_free */ fetch_result_free,
  
  /* msg_fetch */ fetch,
  /* msg_fetch_header */ fetch_header,
  /* msg_fetch_body */ fetch_body,
  /* msg_fetch_size */ fetch_size,
  /* msg_get_bodystructure */ get_bodystructure,
  /* msg_fetch_section */ fetch_section,
  /* msg_fetch_section_header */ fetch_section_header,
  /* msg_fetch_section_mime */ fetch_section_mime,
  /* msg_fetch_section_body */ fetch_section_body,
  /* msg_fetch_envelope */ mailmessage_generic_fetch_envelope,

  /* msg_get_flags */ NULL
};

mailmessage_driver * mime_message_driver = &local_mime_message_driver;
