/*
bctoolbox
Copyright (C) 2016  Belledonne Communications SARL


This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bctoolbox/logging.h"
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _MSC_VER
#ifndef access
#define access _access
#endif
#ifndef fileno
#define fileno _fileno
#endif
#endif

/*
 * Exclude windows and android for bctbx_set_thread_log_level() implementation.
 * Android has lots of bugs around thread local storage and JVM.
 */
#if !defined(_WIN32) && !defined(__ANDROID__)
#define THREAD_LOG_LEVEL_ENABLED 1
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif /* __ANDROID__ */

typedef struct{
	char *domain;
	unsigned int logmask;
#ifdef THREAD_LOG_LEVEL_ENABLED
	int thread_level_set; /* Set to 1 if a thread specific log level has been set. This enables an optimisation
				only for the case of an app that never uses per-thread log levels.*/
	pthread_key_t thread_level_key; /* The key to access the thread specific level. */
#endif
}BctoolboxLogDomain;

#ifdef THREAD_LOG_LEVEL_ENABLED
static void thread_level_key_destroy(void *ptr){
	bctbx_free(ptr);
}
#endif

static BctoolboxLogDomain * bctbx_log_domain_new(const char *domain, unsigned int logmask){
	BctoolboxLogDomain *ld = bctbx_new0(BctoolboxLogDomain, 1);
	ld->domain = domain ? bctbx_strdup(domain) : NULL;
	ld->logmask = logmask;
#ifdef THREAD_LOG_LEVEL_ENABLED
	ld->thread_level_set = FALSE;
	pthread_key_create(&ld->thread_level_key, thread_level_key_destroy);
#endif
	return ld;
}

void bctbx_log_domain_destroy(BctoolboxLogDomain *obj){
#if THREAD_LOG_LEVEL_ENABLED
	pthread_key_delete(obj->thread_level_key);
#endif	
	if (obj->domain) bctbx_free(obj->domain);
	bctbx_free(obj);
}

#if THREAD_LOG_LEVEL_ENABLED
unsigned int bctbx_log_domain_get_thread_log_level_mask(BctoolboxLogDomain *ld){
	unsigned int *specific = (unsigned int*)pthread_getspecific(ld->thread_level_key);
	if (!specific) return 0;
	return *specific;
}
#endif

typedef struct _bctbx_logger_t {
	BctoolboxLogDomain *default_log_domain;
	bctbx_list_t *logv_outs;
	unsigned long log_thread_id;
	bctbx_list_t *log_stored_messages_list;
	bctbx_list_t *log_domains;
	bctbx_mutex_t log_stored_messages_mutex;
	bctbx_mutex_t domains_mutex;
	bctbx_mutex_t log_mutex;
	bctbx_log_handler_t * default_handler;
} bctbx_logger_t;

struct _bctbx_log_handler_t {
	BctbxLogHandlerFunc func;
	BctbxLogHandlerDestroyFunc destroy;
	char *domain; /*domain this log handler is limited to. NULL for all*/
	void* user_info;
};

typedef struct _bctbx_file_log_handler_t {
	char* path;
	char* name;
	uint64_t max_size;
	uint64_t size;
	FILE* file;
	bool_t reopen_requested;
} bctbx_file_log_handler_t;


void bctbx_logv_out_cb(void* user_info, const char *domain, BctbxLogLevel lev, const char *fmt, va_list args);

static void wrapper(void* info,const char *domain, BctbxLogLevel lev, const char *fmt, va_list args) {
	BctbxLogFunc func = (BctbxLogFunc)info;
	if (func) func(domain, lev, fmt,  args);
}

static bctbx_logger_t main_logger = {0};
static bctbx_log_handler_t static_handler = {0};

static bctbx_logger_t * bctbx_get_logger(void){
	if (main_logger.default_log_domain == NULL){
		main_logger.default_log_domain = bctbx_log_domain_new(NULL, BCTBX_LOG_WARNING | BCTBX_LOG_ERROR | BCTBX_LOG_FATAL);
		bctbx_mutex_init(&main_logger.domains_mutex, NULL);
		bctbx_mutex_init(&main_logger.log_mutex, NULL);
		main_logger.default_handler = &static_handler;
		static_handler.func=wrapper;
		static_handler.destroy=(BctbxLogHandlerDestroyFunc)bctbx_logv_out_destroy;
		static_handler.user_info=(void*)bctbx_logv_out;
		bctbx_add_log_handler(&static_handler);
	}
	return &main_logger;
}

void bctbx_init_logger(bool_t create){
	bctbx_get_logger();
}

void bctbx_log_handlers_free(void) {
	bctbx_list_t *handlers = bctbx_list_first_elem(bctbx_get_logger()->logv_outs);
	while (handlers) {
		bctbx_log_handler_t* handler = (bctbx_log_handler_t*)handlers->data;
		handler->destroy(handler);
		handlers = handlers->next;
	}
}

void bctbx_uninit_logger(void){
	/* Calling bctbx_uninit_logger() from a component is dangerous as it will reset the default logger.
	 * it is safer that this function does nothing.*/
#if 0
	bctbx_logger_t * logger = bctbx_get_logger();
	bctbx_logv_flush();
	bctbx_mutex_destroy(&logger->domains_mutex);
	bctbx_mutex_destroy(&logger->log_mutex);
	bctbx_log_handlers_free();
	logger->logv_outs = bctbx_list_free(logger->logv_outs);
	logger->log_domains = bctbx_list_free_with_data(logger->log_domains, (void (*)(void*))bctbx_log_domain_destroy);
	bctbx_log_domain_destroy(logger->default_log_domain);
	logger->default_log_domain = NULL;
#endif
}

bctbx_log_handler_t* bctbx_create_log_handler(BctbxLogHandlerFunc func, BctbxLogHandlerDestroyFunc destroy, void* user_info) {
	bctbx_log_handler_t* handler = (bctbx_log_handler_t*)bctbx_malloc0(sizeof(bctbx_log_handler_t));
	handler->func = func;
	handler->destroy = destroy;
	handler->user_info = user_info;
	return handler;
}

void bctbx_log_handler_set_user_data(bctbx_log_handler_t* log_handler, void* user_data) {
	log_handler->user_info = user_data;
}
void *bctbx_log_handler_get_user_data(const bctbx_log_handler_t* log_handler) {
	return log_handler->user_info;
}

void bctbx_log_handler_set_domain(bctbx_log_handler_t * log_handler, const char *domain) {
	if (log_handler->domain) bctbx_free(log_handler->domain);
	if (domain) {
		log_handler->domain = bctbx_strdup(domain);
	} else {
		log_handler->domain = NULL ;
	}
	
}
bctbx_log_handler_t* bctbx_create_file_log_handler(uint64_t max_size, const char* path, const char* name) {
	bctbx_log_handler_t *handler = NULL;
	bctbx_file_log_handler_t *filehandler = NULL;
	char *full_name = bctbx_strdup_printf("%s/%s", path, name);
	struct stat buf = {0};

	FILE *f = fopen(full_name, "a");
	if (f == NULL) {
		fprintf(stderr, "error while opening '%s': %s\n", full_name, strerror(errno));
		goto end;
	}
	if (stat(full_name, &buf) != 0) {
		fprintf(stderr, "error while gathering info about '%s': %s", full_name, strerror(errno));
		goto end;
	}

	filehandler = bctbx_new0(bctbx_file_log_handler_t, 1);
	filehandler->max_size = max_size;
	filehandler->size = buf.st_size;
	filehandler->path = bctbx_strdup(path);
	filehandler->name = bctbx_strdup(name);
	filehandler->file = f;

	handler = bctbx_new0(bctbx_log_handler_t, 1);
	handler->func = bctbx_logv_file;
	handler->destroy = bctbx_logv_file_destroy;
	handler->user_info = filehandler;

end:
	bctbx_free(full_name);
	return handler;
}

void bctbx_file_log_handler_reopen(bctbx_log_handler_t *file_log_handler) {
	bctbx_file_log_handler_t *filehandler = (bctbx_file_log_handler_t *)file_log_handler->user_info;
	bctbx_logger_t *logger = bctbx_get_logger();
	bctbx_mutex_lock(&logger->log_mutex);
	filehandler->reopen_requested = TRUE;
	bctbx_mutex_unlock(&logger->log_mutex);
}

/**
*@param func: your logging function, compatible with the BctoolboxLogFunc prototype.
*
**/
void bctbx_add_log_handler(bctbx_log_handler_t* handler){
	bctbx_logger_t *logger = bctbx_get_logger();
	if (handler && !bctbx_list_find(logger->logv_outs, handler))
		logger->logv_outs = bctbx_list_append(logger->logv_outs, (void*)handler);
	/*else, already in*/
}

void bctbx_remove_log_handler(bctbx_log_handler_t* handler){
	bctbx_logger_t *logger = bctbx_get_logger();
	logger->logv_outs = bctbx_list_remove(logger->logv_outs,  handler);
	handler->destroy(handler);
	return;
}


void bctbx_set_log_handler(BctbxLogFunc func){
	bctbx_set_log_handler_for_domain(func,NULL);
}

void bctbx_set_log_handler_for_domain(BctbxLogFunc func, const char* domain){
	bctbx_log_handler_t *h = bctbx_get_logger()->default_handler;
	h->user_info=(void*)func;
	bctbx_log_handler_set_domain(h, domain);
}

void bctbx_set_log_file(FILE* f){
	static bctbx_file_log_handler_t filehandler;
	static bctbx_log_handler_t handler;
	handler.func=bctbx_logv_file;
	handler.destroy=(BctbxLogHandlerDestroyFunc)bctbx_logv_file_destroy;
	filehandler.max_size = -1;
	filehandler.file = f;
	handler.user_info=(void*) &filehandler;
	bctbx_add_log_handler(&handler);
}

bctbx_list_t* bctbx_get_log_handlers(void){
	return bctbx_get_logger()->logv_outs;
}

static BctoolboxLogDomain * get_log_domain(const char *domain){
	bctbx_list_t *it;
	bctbx_logger_t *logger = bctbx_get_logger();

	if (domain == NULL) return logger->default_log_domain;
	for (it = logger->log_domains; it != NULL; it = bctbx_list_next(it)) {
		BctoolboxLogDomain *ld = (BctoolboxLogDomain*)bctbx_list_get_data(it);
		if (ld->domain && strcmp(ld->domain, domain) == 0 ){
			return ld;
		}
	}
	return NULL;
}

static BctoolboxLogDomain *get_log_domain_rw(const char *domain){
	BctoolboxLogDomain *ret;
	bctbx_logger_t *logger = bctbx_get_logger();
	
	ret = get_log_domain(domain);
	if (ret) return ret;
	/*it does not exist, hence create it by taking the mutex*/
	bctbx_mutex_lock(&logger->domains_mutex);
	ret = get_log_domain(domain);
	if (!ret){
		ret = bctbx_log_domain_new(domain, logger->default_log_domain->logmask);
		logger->log_domains = bctbx_list_prepend(logger->log_domains, ret);
	}
	bctbx_mutex_unlock(&logger->domains_mutex);
	return ret;
}

/**
* @ param levelmask a mask of BCTBX_DEBUG, BCTBX_MESSAGE, BCTBX_WARNING, BCTBX_ERROR
* BCTBX_FATAL .
**/
void bctbx_set_log_level_mask(const char *domain, int levelmask){
	get_log_domain_rw(domain)->logmask = levelmask;
}

static unsigned int level_to_mask(BctbxLogLevel level){
	unsigned int levelmask = BCTBX_LOG_FATAL;
	if (level<=BCTBX_LOG_ERROR){
		levelmask |= BCTBX_LOG_ERROR;
	}
	if (level<=BCTBX_LOG_WARNING){
		levelmask |= BCTBX_LOG_WARNING;
	}
	if (level<=BCTBX_LOG_MESSAGE){
		levelmask |= BCTBX_LOG_MESSAGE;
	}
	if (level<=BCTBX_LOG_TRACE)	{
		levelmask |= BCTBX_LOG_TRACE;
	}
	if (level<=BCTBX_LOG_DEBUG){
		levelmask |= BCTBX_LOG_DEBUG;
	}
	return levelmask;
}

void bctbx_set_log_level(const char *domain, BctbxLogLevel level){
	bctbx_set_log_level_mask(domain, level_to_mask(level));
}

unsigned int bctbx_get_log_level_mask(const char *domain) {
	BctoolboxLogDomain *ld = get_log_domain(domain);
	if (!ld) ld = bctbx_get_logger()->default_log_domain;
	return ld->logmask;
}

int bctbx_log_level_enabled(const char *domain, BctbxLogLevel level){
	unsigned int logmask = 0;
	BctoolboxLogDomain *ld = get_log_domain(domain);
	if (!ld) ld = bctbx_get_logger()->default_log_domain;
	
#ifdef THREAD_LOG_LEVEL_ENABLED
	if (ld->thread_level_set) logmask = bctbx_log_domain_get_thread_log_level_mask(ld);
#endif
	if (logmask == 0) logmask = ld->logmask; /* if there is no thread specific log level, revert to global log level.*/
	return (logmask & (unsigned int)level) != 0;
}

void bctbx_set_log_thread_id(unsigned long thread_id) {
	bctbx_logger_t *logger = bctbx_get_logger();
	if (thread_id == 0) {
		bctbx_logv_flush();
		bctbx_mutex_destroy(&logger->log_stored_messages_mutex);
	} else {
		bctbx_mutex_init(&logger->log_stored_messages_mutex, NULL);
	}
	logger->log_thread_id = thread_id;
}

char * bctbx_strdup_vprintf(const char *fmt, va_list ap)
{
/* Guess we need no more than 100 bytes. */
	int n, size = 200;
	char *p,*np;
#ifndef _WIN32
	va_list cap;/*copy of our argument list: a va_list cannot be re-used (SIGSEGV on linux 64 bits)*/
#endif
	if ((p = (char *) bctbx_malloc (size)) == NULL)
		return NULL;
	while (1){
/* Try to print in the allocated space. */
#ifndef _WIN32
		va_copy(cap,ap);
		n = vsnprintf (p, size, fmt, cap);
		va_end(cap);
#else
		/*this works on 32 bits, luckily*/
		n = vsnprintf (p, size, fmt, ap);
#endif
		/* If that worked, return the string. */
		if (n > -1 && n < size)
			return p;
//printf("Reallocing space.\n");
		/* Else try again with more space. */
		if (n > -1)	/* glibc 2.1 */
			size = n + 1;	/* precisely what is needed */
		else		/* glibc 2.0 */
			size *= 2;	/* twice the old size */
		if ((np = (char *) bctbx_realloc (p, size)) == NULL)
		{
			free(p);
			return NULL;
		} else {
			p = np;
		}
	}
}

char *bctbx_strdup_printf(const char *fmt,...){
	char *ret;
	va_list args;
	va_start (args, fmt);
	ret=bctbx_strdup_vprintf(fmt, args);
	va_end (args);
	return ret;
}

char * bctbx_strcat_vprintf(char* dst, const char *fmt, va_list ap){
	char *ret;
	size_t dstlen, retlen;

	ret=bctbx_strdup_vprintf(fmt, ap);
	if (!dst) return ret;

	dstlen = strlen(dst);
	retlen = strlen(ret);

	if ((dst = bctbx_realloc(dst, dstlen+retlen+1)) != NULL){
		strcat(dst,ret);
		bctbx_free(ret);
		return dst;
	} else {
		bctbx_free(ret);
		return NULL;
	}
}

char *bctbx_strcat_printf(char* dst, const char *fmt,...){
	char *ret;
	va_list args;
	va_start (args, fmt);
	ret=bctbx_strcat_vprintf(dst, fmt, args);
	va_end (args);
	return ret;
}

#if	defined(_WIN32) || defined(_WIN32_WCE)
#define ENDLINE "\r\n"
#else
#define ENDLINE "\n"
#endif

typedef struct {
	int level;
	char *msg;
	char *domain;
} bctbx_stored_log_t;

void _bctbx_logv_flush(int dummy, ...) {
	bctbx_list_t *elem;
	bctbx_list_t *msglist;
	va_list empty_va_list;
	va_start(empty_va_list, dummy);
	
	bctbx_logger_t *logger = bctbx_get_logger();
	
	bctbx_mutex_lock(&logger->log_stored_messages_mutex);
	msglist = logger->log_stored_messages_list;
	logger->log_stored_messages_list = NULL;
	bctbx_mutex_unlock(&logger->log_stored_messages_mutex);
	for (elem = msglist; elem != NULL; elem = bctbx_list_next(elem)) {
		bctbx_stored_log_t *l = (bctbx_stored_log_t *)bctbx_list_get_data(elem);
		bctbx_list_t *handlers = bctbx_list_first_elem(logger->logv_outs);
#ifdef _WIN32
		while (handlers) {
			bctbx_log_handler_t* handler = (bctbx_log_handler_t*)handlers->data;
			if(handler) {
				va_list cap;
				va_copy(cap, empty_va_list);
				handler->func(handler->user_info, l->domain, l->level, l->msg, cap);
				va_end(cap);
			}
			handlers = handlers->next;
		}
#else
		
		while (handlers) {
			bctbx_log_handler_t* handler = (bctbx_log_handler_t*)handlers->data;
			if(handler) {
				va_list cap;
				va_copy(cap, empty_va_list);
				handler->func(handler->user_info, l->domain, l->level, l->msg, cap);
				va_end(cap);
			}
			handlers = handlers->next;
		}
		
#endif
		if (l->domain) bctbx_free(l->domain);
		bctbx_free(l->msg);
		bctbx_free(l);
	}
	bctbx_list_free(msglist);
	va_end(empty_va_list);
}

void bctbx_logv_flush(void) {
	_bctbx_logv_flush(0);
}

void bctbx_logv(const char *domain, BctbxLogLevel level, const char *fmt, va_list args) {
	bctbx_logger_t *logger = bctbx_get_logger();
	
	if ((logger->logv_outs != NULL) && bctbx_log_level_enabled(domain, level)) {
		if (logger->log_thread_id == 0) {
			bctbx_list_t *handlers = bctbx_list_first_elem(logger->logv_outs);
			while (handlers) {
				bctbx_log_handler_t* handler = (bctbx_log_handler_t*)handlers->data;
				if(handler && (!handler->domain || !domain || strcmp(handler->domain,domain)==0)) {
					va_list tmp;
					va_copy(tmp, args);
					handler->func(handler->user_info, domain, level, fmt, tmp);
					va_end(tmp);
				}
				handlers = handlers->next;
			}
		} else if (logger->log_thread_id == bctbx_thread_self()) {
			bctbx_list_t *handlers;
			bctbx_logv_flush();
			handlers = bctbx_list_first_elem(logger->logv_outs);
			while (handlers) {
				bctbx_log_handler_t* handler = (bctbx_log_handler_t*)handlers->data;
				if(handler && (!handler->domain || !domain || strcmp(handler->domain,domain)==0)) {
					va_list tmp;
					va_copy(tmp, args);
					handler->func(handler->user_info, domain, level, fmt, tmp);
					va_end(tmp);
				}
				handlers = handlers->next;
			}
		} else {
			bctbx_stored_log_t *l = bctbx_new(bctbx_stored_log_t, 1);
			l->domain = domain ? bctbx_strdup(domain) : NULL;
			l->level = level;
			l->msg = bctbx_strdup_vprintf(fmt, args);
			bctbx_mutex_lock(&logger->log_stored_messages_mutex);
			logger->log_stored_messages_list = bctbx_list_append(logger->log_stored_messages_list, l);
			bctbx_mutex_unlock(&logger->log_stored_messages_mutex);
		}
	}
#if !defined(_WIN32_WCE)
	if (level == BCTBX_LOG_FATAL) {
		bctbx_logv_flush();
#ifdef __ANDROID__
        //Act as a flush + abort
		__android_log_assert(NULL, NULL, "%s", "Aborting");
#else
		abort();
#endif
	}
#endif
}
void bctbx_logv_out( const char *domain, BctbxLogLevel lev, const char *fmt, va_list args){
	bctbx_logv_out_cb(NULL, domain, lev, fmt, args);
}
/*This function does the default formatting and output to file*/
void bctbx_logv_out_cb(void* user_info, const char *domain, BctbxLogLevel lev, const char *fmt, va_list args){
	const char *lname="undef";
	char *msg;
	struct timeval tp;
	struct tm *lt;
#ifndef _WIN32
	struct tm tmbuf;
#endif
	time_t tt;
	FILE *std = stdout;
	bctbx_gettimeofday(&tp,NULL);
	tt = (time_t)tp.tv_sec;

#ifdef _WIN32
	lt = localtime(&tt);
#else
	lt = localtime_r(&tt,&tmbuf);
#endif

	switch(lev){
		case BCTBX_LOG_DEBUG:
			lname = "debug";
		break;
		case BCTBX_LOG_MESSAGE:
			lname = "message";
		break;
		case BCTBX_LOG_WARNING:
			lname = "warning";
		break;
		case BCTBX_LOG_ERROR:
			lname = "error";
			std = stderr;
		break;
		case BCTBX_LOG_FATAL:
			lname = "fatal";
			std = stderr;
		break;
		default:
			lname = "badlevel";
	}

	msg=bctbx_strdup_vprintf(fmt,args);
#if defined(_MSC_VER) && !defined(_WIN32_WCE)
#ifndef _UNICODE
	OutputDebugStringA(msg);
	OutputDebugStringA("\r\n");
#else
	{
		size_t len=strlen(msg);
		wchar_t *tmp=(wchar_t*)bctbx_malloc0((len+1)*sizeof(wchar_t));
		mbstowcs(tmp,msg,len);
		OutputDebugStringW(tmp);
		OutputDebugStringW(L"\r\n");
		bctbx_free(tmp);
	}
#endif
#endif
	fprintf(std,"%i-%.2i-%.2i %.2i:%.2i:%.2i:%.3i %s-%s-%s" ENDLINE
			,1900+lt->tm_year,1+lt->tm_mon,lt->tm_mday,lt->tm_hour,lt->tm_min,lt->tm_sec
		,(int)(tp.tv_usec/1000), (domain?domain:"bctoolbox"), lname, msg);
	fflush(std);
	bctbx_free(msg);
}

void bctbx_logv_out_destroy(bctbx_log_handler_t* handler) {
	handler->user_info=NULL;
}

static int _try_open_log_collection_file(bctbx_file_log_handler_t *filehandler) {
	struct stat statbuf;
	char *log_filename;

	log_filename = bctbx_strdup_printf("%s/%s",
		filehandler->path,
		filehandler->name);
	filehandler->file = fopen(log_filename, "a");
	bctbx_free(log_filename);
	if (filehandler->file == NULL) return -1;

	fstat(fileno(filehandler->file), &statbuf);
	if ((uint64_t)statbuf.st_size > filehandler->max_size) {
		fclose(filehandler->file);
		return -1;
	}

	filehandler->size = statbuf.st_size;
	return 0;
}

static void _rotate_log_collection_files(bctbx_file_log_handler_t *filehandler) {
	char *log_filename;
	char *log_filename2;
	char *file_no_extension = bctbx_strdup(filehandler->name);
	char *extension = strrchr(file_no_extension, '.');
	char *extension2 = bctbx_strdup(extension);
	int n = 1;
	file_no_extension[extension - file_no_extension] = '\0';

	log_filename = bctbx_strdup_printf("%s/%s_1%s",
		filehandler->path,
		file_no_extension,
		extension2);
	while(access(log_filename, F_OK) != -1) {
		// file exists
		n++;
		bctbx_free(log_filename);
		log_filename = bctbx_strdup_printf("%s/%s_%d%s",
		filehandler->path,
		file_no_extension,
		n,
		extension2);
	}
	
	while(n > 1) {
		bctbx_free(log_filename);
		log_filename = bctbx_strdup_printf("%s/%s_%d%s",
		filehandler->path,
		file_no_extension,
		n-1,
		extension2);
		log_filename2 = bctbx_strdup_printf("%s/%s_%d%s",
		filehandler->path,
		file_no_extension,
		n,
		extension2);

		n--;
		rename(log_filename, log_filename2);
		bctbx_free(log_filename2);
	}
	bctbx_free(log_filename);
	log_filename = bctbx_strdup_printf("%s/%s",
	filehandler->path,
	filehandler->name);
	log_filename2 = bctbx_strdup_printf("%s/%s_1%s",
	filehandler->path,
	file_no_extension,
	extension2);
	rename(log_filename, log_filename2);
	bctbx_free(log_filename);
	bctbx_free(log_filename2);
	bctbx_free(extension2);
	bctbx_free(file_no_extension);
}

static void _open_log_collection_file(bctbx_file_log_handler_t *filehandler) {
	if (_try_open_log_collection_file(filehandler) < 0) {
		_rotate_log_collection_files(filehandler);
		_try_open_log_collection_file(filehandler);
	}
}

static void _close_log_collection_file(bctbx_file_log_handler_t *filehandler) {
	if (filehandler->file) {
		fclose(filehandler->file);
		filehandler->file = NULL;
		filehandler->size = 0;
	}
}

void bctbx_logv_file(void* user_info, const char *domain, BctbxLogLevel lev, const char *fmt, va_list args){
	const char *lname="undef";
	char *msg = NULL;
	struct timeval tp;
	struct tm *lt;
#ifndef _WIN32
	struct tm tmbuf;
#endif
	time_t tt;
	int ret = -1;
	bctbx_file_log_handler_t *filehandler = (bctbx_file_log_handler_t *) user_info;
	bctbx_logger_t *logger = bctbx_get_logger();
	
	bctbx_mutex_lock(&logger->log_mutex);
	FILE *f = filehandler ? filehandler->file : stdout;
	bctbx_gettimeofday(&tp,NULL);
	tt = (time_t)tp.tv_sec;

#ifdef _WIN32
	lt = localtime(&tt);
#else
	lt = localtime_r(&tt,&tmbuf);
#endif

	if(!f) goto end;

	switch(lev){
		case BCTBX_LOG_DEBUG:
			lname = "debug";
		break;
		case BCTBX_LOG_MESSAGE:
			lname = "message";
		break;
		case BCTBX_LOG_WARNING:
			lname = "warning";
		break;
		case BCTBX_LOG_ERROR:
			lname = "error";
		break;
		case BCTBX_LOG_FATAL:
			lname = "fatal";
		break;
		default:
			lname = "badlevel";
	}

	msg=bctbx_strdup_vprintf(fmt,args);
#if defined(_MSC_VER) && !defined(_WIN32_WCE)
#ifndef _UNICODE
	OutputDebugStringA(msg);
	OutputDebugStringA("\r\n");
#else
	{
		size_t len=strlen(msg);
		wchar_t *tmp=(wchar_t*)bctbx_malloc0((len+1)*sizeof(wchar_t));
		mbstowcs(tmp,msg,len);
		OutputDebugStringW(tmp);
		OutputDebugStringW(L"\r\n");
		bctbx_free(tmp);
	}
#endif
#endif
	ret = fprintf(f,"%i-%.2i-%.2i %.2i:%.2i:%.2i:%.3i %s-%s-%s" ENDLINE
			,1900+lt->tm_year,1+lt->tm_mon,lt->tm_mday,lt->tm_hour,lt->tm_min,lt->tm_sec
		,(int)(tp.tv_usec/1000), (domain?domain:"bctoolbox"), lname, msg);
	fflush(f);

	/* reopen the log file when either the size limit has been exceeded, or reopen has been required
	   by the user. Reopening a log file that has reached the size limit automatically trigger log rotation
	   while opening. */
	if (filehandler) {
		bool_t reopen_requested = filehandler->reopen_requested;
		if (filehandler->max_size > 0 && ret > 0) {
			filehandler->size += ret;
			reopen_requested = reopen_requested || filehandler->size > filehandler->max_size;
		}
		if (reopen_requested) {
			_close_log_collection_file(filehandler);
			_open_log_collection_file(filehandler);
			filehandler->reopen_requested = FALSE;
		}
	}

end:
	bctbx_mutex_unlock(&logger->log_mutex);
	if (msg) bctbx_free(msg);
}

void bctbx_logv_file_destroy(bctbx_log_handler_t* handler) {
	bctbx_file_log_handler_t *filehandler = (bctbx_file_log_handler_t *) handler->user_info;
	fclose(filehandler->file);
	bctbx_free(filehandler->path);
	bctbx_free(filehandler->name);
	bctbx_logv_out_destroy(handler);
}

void bctbx_set_thread_log_level(const char *domain, BctbxLogLevel level){
#ifdef THREAD_LOG_LEVEL_ENABLED
	BctoolboxLogDomain * ld = get_log_domain(domain);
	unsigned int *specific = (unsigned int*)pthread_getspecific(ld->thread_level_key);
	if (!specific) specific = bctbx_new0(unsigned int, 1);
	*specific = level_to_mask(level);
	pthread_setspecific(ld->thread_level_key, specific);
	ld->thread_level_set = TRUE;
#endif
}


void bctbx_clear_thread_log_level(const char *domain){
#ifdef THREAD_LOG_LEVEL_ENABLED
	BctoolboxLogDomain * ld = get_log_domain(domain);
	unsigned int *specific = (unsigned int*)pthread_getspecific(ld->thread_level_key);
	if (specific) *specific = 0;
#endif
}


#ifdef __QNX__
#include <slog2.h>

static bool_t slog2_registered = FALSE;
static slog2_buffer_set_config_t slog2_buffer_config;
static slog2_buffer_t slog2_buffer_handle[2];

void bctbx_qnx_log_handler(const char *domain, BctbxLogLevel lev, const char *fmt, va_list args) {
	uint8_t severity = SLOG2_DEBUG1;
	uint8_t buffer_idx = 1;
	char* msg;

	if (slog2_registered != TRUE) {
		slog2_buffer_config.buffer_set_name = domain;
		slog2_buffer_config.num_buffers = 2;
		slog2_buffer_config.verbosity_level = SLOG2_DEBUG2;
		slog2_buffer_config.buffer_config[0].buffer_name = "hi_rate";
		slog2_buffer_config.buffer_config[0].num_pages = 6;
		slog2_buffer_config.buffer_config[1].buffer_name = "lo_rate";
		slog2_buffer_config.buffer_config[1].num_pages = 2;
		if (slog2_register(&slog2_buffer_config, slog2_buffer_handle, 0) == 0) {
			slog2_registered = TRUE;
		} else {
			fprintf(stderr, "Error registering slogger2 buffer!\n");
			return;
		}
	}

	switch(lev){
		case BCTBX_LOG_DEBUG:
			severity = SLOG2_DEBUG1;
		break;
		case BCTBX_LOG_MESSAGE:
			severity = SLOG2_INFO;
		break;
		case BCTBX_LOG_WARNING:
			severity = SLOG2_WARNING;
		break;
		case BCTBX_LOG_ERROR:
			severity = SLOG2_ERROR;
		break;
		case BCTBX_LOG_FATAL:
			severity = SLOG2_CRITICAL;
		break;
		default:
			severity = SLOG2_CRITICAL;
	}

	msg = bctbx_strdup_vprintf(fmt,args);
	slog2c(slog2_buffer_handle[buffer_idx], 0, severity, msg);
}

#endif /* __QNX__ */
