#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <libetpan/libetpan.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>
#ifdef WIN32
#	include "win_etpan.h"
#else
#	include <sys/mman.h>
#endif
#include <stdlib.h>
#include <string.h>

int get_content_of_file(char * filename, char ** p_content, size_t * p_length)
{
  int r;
  struct stat stat_buf;
  int fd;
  char * content;
  int nread;

  r = stat(filename, &stat_buf);
  if (r < 0)
    goto err;
  
  content = malloc(stat_buf.st_size + 1);
  if (content == NULL)
    goto err;
  
  fd = open(filename, O_RDONLY);
  if (fd < 0)
    goto free;
  
  nread = 0;
  do
    {
      r = read (fd, content + nread, stat_buf.st_size - nread);
      if (r > 0)
	nread += r;
    }
  while (nread < stat_buf.st_size && (r > 0 || (r < 0 && errno == EAGAIN)));

  if (r < 0)
    goto close;

  content[stat_buf.st_size] = '\0';
  close(fd);
  
  * p_content = content;
  * p_length = stat_buf.st_size;
  
  return 0;
  
 close:
  close(fd);
 free:
  free(content);
 err:
  return -1;
}

int main(int argc, char ** argv)
{
  char * content;
  size_t length;
  mailmessage * msg;
  int r;
  struct mailprivacy * privacy;
  struct mailmime * mime;
  int col;
  
  privacy = mailprivacy_new("/Users/hoa/tmp", 1);
  if (privacy == NULL) {
    goto err;
  }
  
  r = mailprivacy_gnupg_init(privacy);
  if (r != MAIL_NO_ERROR) {
    goto free_privacy;
  }

  r = mailprivacy_smime_init(privacy);
  mailprivacy_smime_set_cert_dir(privacy,
      "/Users/hoa/LibEtPan/libetpan/tests/keys/cert");
  mailprivacy_smime_set_CA_dir(privacy,
      "/Users/hoa/LibEtPan/libetpan/tests/keys/ca");
  mailprivacy_smime_set_private_keys_dir(privacy,
      "/Users/hoa/LibEtPan/libetpan/tests/keys/private");

  mailprivacy_gnupg_set_encryption_id(privacy, "xxxx@xxxx", "coin");
  mailprivacy_smime_set_encryption_id(privacy, "xxxx@xxxx", "coin");
  
  if (argc < 2) {
    fprintf(stderr, "syntax: decrypt [message]\n");
    goto done_gpg;
  }
  
  r = get_content_of_file(argv[1], &content, &length);
  if (r < 0) {
    fprintf(stderr, "file not found %s\n", argv[1]);
    goto done_gpg;
  }
  
  msg = data_message_init(content, length);
  if (msg == NULL) {
    fprintf(stderr, "unexpected error\n");
    goto free_content;
  }
  
  r = mailprivacy_msg_get_bodystructure(privacy, msg, &mime);
  if (r != MAIL_NO_ERROR) {
    fprintf(stderr, "unexpected error\n");
    goto free_content;
  }
  
  mailmime_write(stdout, &col, mime);
  
  {
    clist * id_list;
    clistiter * iter;
    
    id_list = mailprivacy_gnupg_encryption_id_list(privacy, msg);
    if (id_list != NULL) {
      for(iter = clist_begin(id_list) ; iter != NULL ; iter = clist_next(iter)) {
        char * str;
        
        str = clist_content(iter);
        fprintf(stderr, "%s\n", str);
      }
    }
  }
  
  {
    clist * id_list;
    clistiter * iter;
    
    id_list = mailprivacy_smime_encryption_id_list(privacy, msg);
    if (id_list != NULL) {
      for(iter = clist_begin(id_list) ; iter != NULL ; iter = clist_next(iter)) {
        char * str;
        
        str = clist_content(iter);
        fprintf(stderr, "%s\n", str);
      }
    }
  }
  
  mailprivacy_gnupg_encryption_id_list_clear(privacy, msg);
  
  mailmessage_free(msg);
  
  free(content);
  mailprivacy_smime_done(privacy);
  mailprivacy_gnupg_done(privacy);
  mailprivacy_free(privacy);
  
  exit(EXIT_SUCCESS);
  
 free_content:
  free(content);
 done_gpg:
  mailprivacy_gnupg_done(privacy);
 free_privacy:
  mailprivacy_free(privacy);
 err:
  exit(EXIT_FAILURE);
}
