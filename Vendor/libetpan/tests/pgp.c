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

int mailmessage_encrypt(struct mailprivacy * privacy,
    struct mailmessage * msg,
    char * protocol, char * encryption_method)
{
  int r;
  int res;
  struct mailmime * mime;
  struct mailmime * encrypted_mime;
  struct mailmime * part_to_encrypt;
  
  r = mailprivacy_msg_get_bodystructure(privacy, msg, &mime);
  if (r != MAIL_NO_ERROR) {
    res = r;
    goto err;
  }
  
  part_to_encrypt = mime->mm_data.mm_message.mm_msg_mime;
  
  r = mailprivacy_encrypt_msg(privacy, protocol, encryption_method,
      msg, part_to_encrypt, &encrypted_mime);
  if (r != MAIL_NO_ERROR) {
    res = r;
    goto err;
  }
  
  mime->mm_data.mm_message.mm_msg_mime = encrypted_mime;
  encrypted_mime->mm_parent = mime;
  part_to_encrypt->mm_parent = NULL;
  mailmime_free(part_to_encrypt);
  
  return MAIL_NO_ERROR;
  
 err:
  return res;
}

int main(int argc, char ** argv)
{
  char * content;
  size_t length;
  mailmessage * msg;
  int r;
  struct mailprivacy * privacy;
  int col;
  
  privacy = mailprivacy_new("/Users/hoa/tmp", 1);
  if (privacy == NULL) {
    goto err;
  }
  
  r = mailprivacy_gnupg_init(privacy);
  if (r != MAIL_NO_ERROR) {
    goto free_privacy;
  }
  
  if (argc < 2) {
    fprintf(stderr, "syntax: pgp [message]\n");
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
  
  r = mailmessage_encrypt(privacy, msg, "pgp", "encrypted");
  if (r != MAIL_NO_ERROR) {
    fprintf(stderr, "cannot encrypt\n");
    goto free_content;
  }
  
  col = 0;
  mailmime_write(stdout, &col, msg->msg_mime);
  
  mailmessage_free(msg);
  
  free(content);
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
