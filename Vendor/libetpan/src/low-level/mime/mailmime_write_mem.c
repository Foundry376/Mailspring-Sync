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
 * $Id: mailmime_write_mem.c,v 1.6 2007/07/12 14:45:54 smarinier Exp $
 */

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "mailmime_write_mem.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_SYS_MMAN_H
#	include <sys/mman.h>
#endif

#include "mailmime_content.h"
#include "mailmime_types_helper.h"
#include "mailmime_write_generic.h"

static int do_write(void * data, const char * str, size_t length)
{
  MMAPString * f;
  
  f = data;
  
  if (mmap_string_append_len(f, str, length) == NULL)
    return 0;
  else
    return (int) length;
}

int mailmime_fields_write_mem(MMAPString * f, int * col,
			  struct mailmime_fields * fields)
{
  return mailmime_fields_write_driver(do_write, f, col, fields);
}

int mailmime_content_write_mem(MMAPString * f, int * col,
			   struct mailmime_content * content)
{
  return mailmime_content_write_driver(do_write, f, col, content);
}

int mailmime_content_type_write_mem(MMAPString * f, int * col,
				struct mailmime_content * content)
{
  return mailmime_content_type_write_driver(do_write, f, col, content);
}

int mailmime_write_mem(MMAPString * f, int * col,
		   struct mailmime * build_info)
{
  return mailmime_write_driver(do_write, f, col, build_info);
}

int mailmime_quoted_printable_write_mem(MMAPString * f, int * col, int istext,
    const char * text, size_t size)
{
  return mailmime_quoted_printable_write_driver(do_write, f, col,
      istext, text, size);
}

int mailmime_base64_write_mem(MMAPString * f, int * col,
    const char * text, size_t size)
{
  return mailmime_base64_write_driver(do_write, f, col, text, size);
}

int mailmime_data_write_mem(MMAPString * f, int * col,
    struct mailmime_data * data,
    int istext)
{
  return mailmime_data_write_driver(do_write, f, col, data, istext);
}

