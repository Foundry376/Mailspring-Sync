/*
vfs.c
Copyright (C) 2016 Belledonne Communications SARL

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bctoolbox/vfs.h"
#include "bctoolbox/port.h"
#include "bctoolbox/logging.h"
#include <sys/types.h>
#include <stdarg.h>
#include <errno.h>



/**
 * Opens the file with filename fName, associate it to the file handle pointed
 * by pFile, sets the methods bctbx_io_methods_t to the bcio structure
 * and initializes the file size.
 * Sets the error in pErrSvd if an error occurred while opening the file fName.
 * @param  pVfs    		Pointer to  bctx_vfs  VFS.
 * @param  fName   		Absolute path filename.
 * @param  openFlags    Flags to use when opening the file.
 * @return         		BCTBX_VFS_ERROR if an error occurs, BCTBX_VFS_OK otherwise.
 */
static  int bcOpen(bctbx_vfs_t *pVfs, bctbx_vfs_file_t *pFile, const char *fName, int openFlags);


static bctbx_vfs_t bcVfs = {
	"bctbx_vfs",               /* vfsName */
	bcOpen,						/*xOpen */
};

/* Pointer to default VFS initialized to standard VFS implemented here.*/
static bctbx_vfs_t *pDefaultVfs = &bcVfs;


/**
 * Closes file by closing the associated file descriptor.
 * Sets the error errno in the argument pErrSrvd after allocating it
 * if an error occurrred.
 * @param  pFile 	bctbx_vfs_file_t File handle pointer.
 * @return       	BCTBX_VFS_OK if successful, BCTBX_VFS_ERROR otherwise.
 */
static int bcClose(bctbx_vfs_file_t *pFile) {
	int ret;
	ret = close(pFile->fd);
	if (!ret) {
		ret = BCTBX_VFS_OK;
	} else {
		ret = -errno;
	}
	return ret;
}

/**
 * Repositions the open file offset given by the file descriptor fd from the file handle pFile
 * to the parameter offset, according to whence.
 * @param  pFile  File handle pointer.
 * @param  offset file offset where to set the position to
 * @param  whence Either SEEK_SET, SEEK_CUR,SEEK_END .
 * @return   offset bytes from the beginning of the file, BCTBX_VFS_ERROR otherwise
 */
static off_t bcSeek(bctbx_vfs_file_t *pFile, off_t offset, int whence) {
	off_t ofst;
	if (pFile) {
		ofst = lseek(pFile->fd, offset, whence);
		if (ofst < 0) return -errno;
		return ofst;
	}
	return BCTBX_VFS_ERROR;
}

/**
 * Read count bytes from the open file given by pFile, starting at offset.
 * Sets the error errno in the argument pErrSrvd after allocating it
 * if an error occurrred.
 * @param  pFile  File handle pointer.
 * @param  buf    buffer to write the read bytes to.
 * @param  count  number of bytes to read
 * @param  offset file offset where to start reading
 * @return -errno if erroneous read, number of bytes read (count) on success, 
 *                if the error was something else BCTBX_VFS_ERROR otherwise
 */
static ssize_t bcRead(bctbx_vfs_file_t *pFile, void *buf, size_t count, off_t offset) {
	ssize_t nRead;                      /* Return value from read() */
	if (pFile) {
		if (bctbx_file_seek(pFile, offset, SEEK_SET) == BCTBX_VFS_OK) {
			nRead = bctbx_read(pFile->fd, buf, count);
			/* Error while reading */
			if (nRead < 0) {
				if (errno) return -errno;
			}
			return nRead;
		}
	}
	return BCTBX_VFS_ERROR;
}

/**
 * Writes directly to the open file given through the pFile argument.
 * Sets the error errno in the argument pErrSrvd after allocating it
 * if an error occurrred.
 * @param  p       bctbx_vfs_file_t File handle pointer.
 * @param  buf     Buffer containing data to write
 * @param  count   Size of data to write in bytes
 * @param  offset  File offset where to write to
 * @return         number of bytes written (can be 0), negative value errno if an error occurred.
 */
static ssize_t bcWrite(bctbx_vfs_file_t *p, const void *buf, size_t count, off_t offset) {
	ssize_t nWrite = 0;                 /* Return value from write() */

	if (p) {
		if ((bctbx_file_seek(p, offset, SEEK_SET)) == BCTBX_VFS_OK) {
			nWrite = bctbx_write(p->fd, buf, count);
			if (nWrite > 0) return nWrite;
			else if (nWrite <= 0) {
				if (errno) return -errno;
				return 0;
			}
		}
	}
	return BCTBX_VFS_ERROR;
}

/**
 * Returns the file size associated with the file handle pFile.
 * @param pFile File handle pointer.
 * @return -errno if an error occurred, file size otherwise (can be 0).
 */
static int64_t bcFileSize(bctbx_vfs_file_t *pFile) {
	int rc;                         /* Return code from fstat() call */
	struct stat sStat;              /* Output of fstat() call */
	rc = fstat(pFile->fd, &sStat);
	if (rc != 0) {
		return -errno;
	}
	return sStat.st_size;
}


/*
 ** Truncate a file
 * @param pFile File handle pointer.
 * @param new_size Extends the file with null bytes if it is superiori to the file's size
 *                 truncates the file otherwise.
 * @return -errno if an error occurred, 0 otherwise.
  */
static int bcTruncate(bctbx_vfs_file_t *pFile, int64_t new_size){
	
	int ret;
	
	#if _WIN32
	ret = _chsize(pFile->fd, (long)new_size);
	#else
	ret = ftruncate(pFile->fd, new_size);
	#endif	

	if (ret < 0) {
		return -errno;
	}
	return 0;
}
/**
 * Gets a line of max_len length and stores it to the allocaed buffer s.
 * Reads at most max_len characters from the file descriptor associated with the argument pFile
 * and looks for an end of line character (\r or \n). Stores the line found
 * into the buffer pointed by s.
 * Modifies the open file offset using pFile->offset.
 *
 * @param  pFile   File handle pointer.
 * @param  s       Buffer where to store the line.
 * @param  max_len Maximum number of characters to read in one fetch.
 * @return         size of line read, 0 if empty
 */
static int bcGetLine(bctbx_vfs_file_t *pFile, char *s, int max_len) {
	int64_t ret;
	int sizeofline;
	char *pNextLine = NULL;
	char *pNextLineR = NULL;
	char *pNextLineN = NULL;

	if (pFile->fd == -1) {
		return BCTBX_VFS_ERROR;
	}
	if (s == NULL || max_len < 1) {
		return BCTBX_VFS_ERROR;
	}

	sizeofline = 0;
	s[max_len-1] = '\0';

	/* Read returns 0 if end of file is found */
	ret = bctbx_file_read(pFile, s, max_len - 1, pFile->offset);
	if (ret > 0) {
		pNextLineR = strchr(s, '\r');
		pNextLineN = strchr(s, '\n');
		if ((pNextLineR != NULL) && (pNextLineN != NULL)) pNextLine = MIN(pNextLineR, pNextLineN);
		else if (pNextLineR != NULL) pNextLine = pNextLineR;
		else if (pNextLineN != NULL) pNextLine = pNextLineN;
		if (pNextLine) {
			/* Got a line! */
			*pNextLine = '\0';
			sizeofline = (int)(pNextLine - s + 1);
			if (pNextLine[1] == '\n') sizeofline += 1; /*take into account the \r\n" case*/

			/* offset to next beginning of line*/
			pFile->offset += sizeofline;
		} else {
			/*did not find end of line char, is EOF?*/
			sizeofline = (int)ret;
			pFile->offset += sizeofline;
			s[ret] = '\0';
		}
	} else if (ret < 0) {
		bctbx_error("bcGetLine error");
	}
	return sizeofline;
}

/**
 * Create flags (int) from mode(char*).
 * @param mode Can be r, r+, w+, w
 * @return flags (integer).	 
 */
static int set_flags(const char* mode) {
	int flags = 0 ;                 /* flags to pass to open() call */
	if (strcmp(mode, "r") == 0) {
  		flags =  O_RDONLY;
	} else if ((strcmp(mode, "r+") == 0) || (strcmp(mode, "w+") == 0)) {
		flags =  O_RDWR;
	} else if(strcmp(mode, "w") == 0) {
  		flags =  O_WRONLY;
	}
	return flags | O_CREAT;
}


static const  bctbx_io_methods_t bcio = {
	bcClose,                    /* pFuncClose */
	bcRead,                     /* pFuncRead */
	bcWrite,                    /* pFuncWrite */
	bcTruncate,					/* pFuncTruncate */
	bcFileSize,                 /* pFuncFileSize */
	bcGetLine,
	bcSeek,
};



static int bcOpen(bctbx_vfs_t *pVfs, bctbx_vfs_file_t *pFile, const char *fName, int openFlags) {
	if (pFile == NULL || fName == NULL) {
		return BCTBX_VFS_ERROR;
	}
#if _WIN32
	openFlags |= O_BINARY;
#endif
	
	pFile->fd = open(fName, openFlags, S_IRUSR | S_IWUSR);
	if (pFile->fd == -1) {
		return -errno;
	}

	pFile->pMethods = &bcio;
	return BCTBX_VFS_OK;
}

ssize_t bctbx_file_write(bctbx_vfs_file_t* pFile, const void *buf, size_t count, off_t offset) {
	ssize_t ret;

	if (pFile != NULL) {
		ret = pFile->pMethods->pFuncWrite(pFile, buf, count, offset);
		if (ret == BCTBX_VFS_ERROR) {
			bctbx_error("bctbx_file_write file error");
			return BCTBX_VFS_ERROR;
		} else if (ret < 0) {
			bctbx_error("bctbx_file_write error %s", strerror(-(ret)));
			return BCTBX_VFS_ERROR;
		}
		return ret;
	}
	return BCTBX_VFS_ERROR;
}

static int file_open(bctbx_vfs_t* pVfs, bctbx_vfs_file_t* pFile, const char *fName, const int oflags) {
	int ret = BCTBX_VFS_ERROR;
	if (pVfs && pFile ) {
		ret = pVfs->pFuncOpen(pVfs, pFile, fName, oflags);
		if (ret == BCTBX_VFS_ERROR) {
			bctbx_error("bctbx_file_open: Error file handle");
		} else if (ret < 0 ) {
			bctbx_error("bctbx_file_open: Error open %s", strerror(-(ret)));
			ret = BCTBX_VFS_ERROR;
		}
	}
	return ret;
}

bctbx_vfs_file_t* bctbx_file_open(bctbx_vfs_t *pVfs, const char *fName, const char *mode) {
	int ret;
	bctbx_vfs_file_t* p_ret = (bctbx_vfs_file_t*)bctbx_malloc(sizeof(bctbx_vfs_file_t));
	int oflags = set_flags(mode);
	if (p_ret) {
		memset(p_ret, 0, sizeof(bctbx_vfs_file_t));
		ret = file_open(pVfs, p_ret, fName, oflags);
		if (ret == BCTBX_VFS_OK) return p_ret;
	}

	if (p_ret) bctbx_free(p_ret);
	return NULL;
}

bctbx_vfs_file_t* bctbx_file_open2(bctbx_vfs_t *pVfs, const char *fName, const int openFlags) {
	int ret;
	bctbx_vfs_file_t *p_ret = (bctbx_vfs_file_t *)bctbx_malloc(sizeof(bctbx_vfs_file_t));

	if (p_ret) {
		memset(p_ret, 0, sizeof(bctbx_vfs_file_t));
		ret = file_open(pVfs, p_ret, fName, openFlags);
		if (ret == BCTBX_VFS_OK) return p_ret;
	}

	if (p_ret) bctbx_free(p_ret);
	return NULL;
}

ssize_t bctbx_file_read(bctbx_vfs_file_t *pFile, void *buf, size_t count, off_t offset) {
	int ret = BCTBX_VFS_ERROR;
	if (pFile) {
		ret = pFile->pMethods->pFuncRead(pFile, buf, count, offset);
		/*check if error : in this case pErrSvd is initialized*/
		if (ret == BCTBX_VFS_ERROR) {
			bctbx_error("bctbx_file_read: error bctbx_vfs_file_t");
		}
		else if (ret < 0) {
			bctbx_error("bctbx_file_read: Error read %s", strerror(-(ret)));
			ret = BCTBX_VFS_ERROR;
		}
	}
	return ret;
}

int bctbx_file_close(bctbx_vfs_file_t *pFile) {
	int ret = BCTBX_VFS_ERROR;
	if (pFile) {
		ret = pFile->pMethods->pFuncClose(pFile);
		if (ret != 0) {
			bctbx_error("bctbx_file_close: Error %s freeing file handle anyway", strerror(-(ret)));
		}
	}
	bctbx_free(pFile);
	return ret;
}



int64_t bctbx_file_size(bctbx_vfs_file_t *pFile) {
	int64_t ret = BCTBX_VFS_ERROR;
	if (pFile){
		ret = pFile->pMethods->pFuncFileSize(pFile);
		if (ret < 0) bctbx_error("bctbx_file_size: Error file size %s", strerror((int)-(ret)));
	} 
	return ret;
}


int bctbx_file_truncate(bctbx_vfs_file_t *pFile, int64_t size) {
	int ret = BCTBX_VFS_ERROR;
	if (pFile){
		ret = pFile->pMethods->pFuncTruncate(pFile, size);
		if (ret < 0) bctbx_error("bctbx_file_truncate: Error truncate  %s", strerror((int)-(ret)));
	} 
	return ret;
}

ssize_t bctbx_file_fprintf(bctbx_vfs_file_t *pFile, off_t offset, const char *fmt, ...) {
	char *ret = NULL;
	va_list args;
	ssize_t r = BCTBX_VFS_ERROR;
	size_t count = 0;

	va_start(args, fmt);
	ret = bctbx_strdup_vprintf(fmt, args);
	if (ret != NULL) {
		va_end(args);
		count = strlen(ret);

		if (offset != 0) pFile->offset = offset;
		r = bctbx_file_write(pFile, ret, count, pFile->offset);
		bctbx_free(ret);
		if (r > 0) pFile->offset += r;
	}
	return r;
}

off_t bctbx_file_seek(bctbx_vfs_file_t *pFile, off_t offset, int whence) {
	off_t ret = BCTBX_VFS_ERROR;

	if (pFile) {
		ret = pFile->pMethods->pFuncSeek(pFile, offset, whence);
		if (ret < 0) {
			bctbx_error("bctbx_file_seek: Wrong offset %s", strerror((int)-(ret)));
		} else if (ret == offset) {
			ret = BCTBX_VFS_OK;
		}
	} 
	return ret;
}

int bctbx_file_get_nxtline(bctbx_vfs_file_t *pFile, char *s, int maxlen) {
	if (pFile) return pFile->pMethods->pFuncGetLineFromFd(pFile, s, maxlen);
	return BCTBX_VFS_ERROR;
}


void bctbx_vfs_set_default(bctbx_vfs_t *my_vfs) {
	pDefaultVfs = my_vfs;
}

bctbx_vfs_t* bctbx_vfs_get_default(void) {
	return pDefaultVfs;	
}

bctbx_vfs_t* bctbx_vfs_get_standard(void) {
	return &bcVfs;
}

