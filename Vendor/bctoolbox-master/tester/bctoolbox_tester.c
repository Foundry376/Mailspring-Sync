/*
	bctoolbox
	Copyright (C) 2016  Belledonne Communications SARL

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "bctoolbox/logging.h"
#include "bctoolbox_tester.h"

static const char *log_domain = "bctoolbox-tester";

static void log_handler(int lev, const char *fmt, va_list args) {
#ifdef _WIN32
	/* We must use stdio to avoid log formatting (for autocompletion etc.) */
	vfprintf(lev == BCTBX_LOG_ERROR ? stderr : stdout, fmt, args);
	fprintf(lev == BCTBX_LOG_ERROR ? stderr : stdout, "\n");
#else
	va_list cap;
	va_copy(cap,args);
	vfprintf(lev == BCTBX_LOG_ERROR ? stderr : stdout, fmt, cap);
	fprintf(lev == BCTBX_LOG_ERROR ? stderr : stdout, "\n");
	va_end(cap);
#endif

	bctbx_logv(log_domain,lev, fmt, args);
}


void bctoolbox_tester_init(void(*ftester_printf)(int level, const char *fmt, va_list args)) {
	bc_tester_init(log_handler,BCTBX_LOG_ERROR, 0,NULL);
	bc_tester_add_suite(&containers_test_suite);
	bc_tester_add_suite(&utils_test_suite);
#if (HAVE_MBEDTLS | HAVE_POLARSSL)
	bc_tester_add_suite(&crypto_test_suite);
#endif
	bc_tester_add_suite(&parser_test_suite);
}

void bctoolbox_tester_uninit(void) {
	bc_tester_uninit();
}

void bctoolbox_tester_before_each() {
}

int bctoolbox_tester_set_log_file(const char *filename) {
	int res = 0;
	char* dir = bctbx_dirname(filename);
	char* base = bctbx_basename(filename);
	bctbx_message("Redirecting traces to file [%s]", filename);
	bctbx_log_handler_t *filehandler = bctbx_create_file_log_handler(0, dir, base);
	if (filehandler == NULL) {
		res = -1;
		goto end;
	}
	bctbx_add_log_handler(filehandler);

end:
	bctbx_free(dir);
	bctbx_free(base);
	return res;
}


#if !defined(__ANDROID__) && !(defined(BCTBX_WINDOWS_PHONE) || defined(BCTBX_WINDOWS_UNIVERSAL))

static const char* bctoolbox_helper =
		"\t\t\t--verbose\n"
		"\t\t\t--silent\n"
		"\t\t\t--log-file <output log file path>\n";

int main (int argc, char *argv[]) {
	int i;
	int ret;

	bctbx_init_logger(TRUE);
	bctoolbox_tester_init(NULL);

	for(i=1;i<argc;++i){
		if (strcmp(argv[i],"--verbose")==0){
			bctbx_set_log_level(log_domain, BCTBX_LOG_DEBUG);
			bctbx_set_log_level(BCTBX_LOG_DOMAIN,BCTBX_LOG_DEBUG);
		} else if (strcmp(argv[i],"--silent")==0){
			bctbx_set_log_level(log_domain, BCTBX_LOG_FATAL);
		} else if (strcmp(argv[i],"--log-file")==0){
			CHECK_ARG("--log-file", ++i, argc);
			if (bctoolbox_tester_set_log_file(argv[i]) < 0) return -2;
		}else {
			int ret = bc_tester_parse_args(argc, argv, i);
			if (ret>0) {
				i += ret - 1;
				continue;
			} else if (ret<0) {
				bc_tester_helper(argv[0], bctoolbox_helper);
			}
			return ret;
		}
	}
	ret = bc_tester_start(argv[0]);
	bctoolbox_tester_uninit();
	bctbx_uninit_logger();
	return ret;
}

#endif
