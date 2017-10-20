/*
 * File: platform.cpp
 * ------------------
 * This file implements the platform interface by passing commands to
 * a Java back end that manages the display.
 * 
 * @version 2016/03/16
 * - added functions for HTTP server
 * @version 2015/10/21
 * - moved EchoingStreambuf to plainconsole.h/cpp facilitate in/output capture
 * @version 2015/10/08
 * - bug fixes for sending long strings through Java process pipe
 * @version 2015/10/01
 * - fix to check JAVA_HOME environment variable to force Java executable path
 *   (improved compatibility on Windows systems with many JDKs/JREs installed)
 * @version 2015/08/05
 * - added output limit for EchoingStreamBuf to facilitate trimming of infinite
 *   output by runaway programs / infinite loops
 * @version 2015/08/01
 * - added flag for 'headless' mode for use in non-GUI server environments
 * @version 2015/07/05
 * - added EchoingStreamBuf class for use in unified single-file version of library
 * @version 2014/11/25
 * - added methods for checking and unchecking individual autograder test cases
 * @version 2014/11/20
 * - added gwindow clearCanvas method
 * @version 2014/11/15
 * - improvements to autograder unit test GUI
 * @version 2014/11/14
 * - added method to set unit test runtime in MS
 * @version 2014/11/05
 * - added ability to see cerr messages in red on graphical console window
 * @version 2014/10/31
 * - added functions for graphical style checker
 * - added functions to get/check versions of cpp/Java/project
 * @version 2014/10/27
 * - added functions for graphical autograder unit tests
 * - added setVisible for autograder input panel
 * @version 2014/10/14
 * - removed unused fileLogConsole functionality
 * - added code to verify backend spl.jar version compatibility
 * @version 2014/10/08
 * - removed 'using namespace' statement
 * - fixed bug in GBufferedImage.fillRegion back-end call (missing ", " token)
 * - bug fix for filelib openFileDialog call
 * - implemented backend of filelib getTempDirectory
 * - implemented backend of GBufferedImage resize
 * - fixed/completed backend of GOptionPane functions
 * - implemented backend of URL download for iurlstream
 */

#ifdef _WIN32
#  include <windows.h>
#  include <tchar.h>
#  undef MOUSE_EVENT
#  undef KEY_EVENT
#  undef MOUSE_MOVED
#  undef HELP_KEY
#else // _WIN32
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/resource.h>
#  include <dirent.h>
#  include <errno.h>
#  include <pwd.h>
#  include <stdint.h>
#  include <unistd.h>
#endif // _WIN32

#include "platform.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ios>
#include <signal.h>
#include <sstream>
#include <string>
#include <vector>
#include "error.h"
#include "exceptions.h"
#include "filelib.h"
#include "strlib.h"

/* Private data */

static std::string programName;

/* Implementation of the Platform class */

Platform::Platform() {
    /* Empty */
}

Platform::~Platform() {
    /* Empty */
}

/* Unix implementations of filelib.h primitives */

#ifndef _WIN32

// Unix implementation; see Windows implementation elsewhere in this file
bool Platform::filelib_fileExists(std::string filename) {
    struct stat fileInfo;
    return stat(filename.c_str(), &fileInfo) == 0;
}

// Unix implementation; see Windows implementation elsewhere in this file
bool Platform::filelib_isFile(std::string filename) {
    struct stat fileInfo;
    if (stat(filename.c_str(), &fileInfo) != 0) return false;
    return S_ISREG(fileInfo.st_mode) != 0;
}

// Unix implementation; see Windows implementation elsewhere in this file
bool Platform::filelib_isSymbolicLink(std::string filename) {
    struct stat fileInfo;
    if (stat(filename.c_str(), &fileInfo) != 0) return false;
    return S_ISLNK(fileInfo.st_mode) != 0;
}

// Unix implementation; see Windows implementation elsewhere in this file
bool Platform::filelib_isDirectory(std::string filename) {
    struct stat fileInfo;
    if (stat(filename.c_str(), &fileInfo) != 0) return false;
    return S_ISDIR(fileInfo.st_mode) != 0;
}

// Unix implementation; see Windows implementation elsewhere in this file
void Platform::filelib_setCurrentDirectory(std::string path) {
    if (chdir(path.c_str()) == 0) {
        std::string msg = "setCurrentDirectory: ";
        std::string err = std::string(strerror(errno));
        error(msg + err);
    }
}

// Unix implementation; see Windows implementation elsewhere in this file
std::string Platform::filelib_getCurrentDirectory() {
    char *cwd = getcwd(NULL, 0);
    if (cwd == NULL) {
        std::string msg = "getCurrentDirectory: ";
        std::string err = std::string(strerror(errno));
        error(msg + err);
        return "";
    } else {
        std::string result = std::string(cwd);
        free(cwd);
        return result;
    }
}

// Unix implementation; see Windows implementation elsewhere in this file
// http://stackoverflow.com/questions/8087805/
// how-to-get-system-or-user-temp-folder-in-unix-and-windows
std::string Platform::filelib_getTempDirectory() {
    char* dir = getenv("TMPDIR");
    if (!dir) dir = getenv("TMP");
    if (!dir) dir = getenv("TEMP");
    if (!dir) dir = getenv("TEMPDIR");
    if (!dir) return "/tmp";
    return dir;
}

// Unix implementation; see Windows implementation elsewhere in this file
void Platform::filelib_createDirectory(std::string path) {
    if (endsWith(path, "/")) {
        path = path.substr(0, path.length() - 2);
    }
    if (mkdir(path.c_str(), 0777) != 0) {
        if (errno == EEXIST && filelib_isDirectory(path)) return;
        std::string msg = "createDirectory: ";
        std::string err = std::string(strerror(errno));
        error(msg + err);
    }
}

// Unix implementation; see Windows implementation elsewhere in this file
std::string Platform::filelib_getDirectoryPathSeparator() {
    return "/";
}

// Unix implementation; see Windows implementation elsewhere in this file
std::string Platform::filelib_getSearchPathSeparator() {
    return ":";
}

// Unix implementation; see Windows implementation elsewhere in this file
std::string Platform::filelib_expandPathname(std::string filename) {
    if (filename == "") return "";
    size_t len = filename.length();
    if (filename[0] == '~') {
        int spos = 1;
        while (spos < len && filename[spos] != '\\' && filename[spos] != '/') {
            spos++;
        }
        char *homedir = NULL;
        if (spos == 1) {
            homedir = getenv("HOME");
            if (homedir == NULL) {
                homedir = getpwuid(getuid())->pw_dir;
            }
        } else {
            struct passwd *pw = getpwnam(filename.substr(1, spos - 1).c_str());
            if (pw == NULL) {
                error("expandPathname: No such user");
            } else {
                homedir = pw->pw_dir;
            }
        }
        filename = std::string(homedir) + filename.substr(spos);
        len = filename.length();
    }
    for (int i = 0; i < len; i++) {
        if (filename[i] == '\\') filename[i] = '/';
    }
    return filename;
}

// Unix implementation; see Windows implementation elsewhere in this file
void Platform::filelib_listDirectory(std::string path, std::vector<std::string>& list) {
    if (path == "") path = ".";
    DIR *dir = opendir(path.c_str());
    if (dir == NULL) error(std::string("listDirectory: Can't open ") + path);
    list.clear();
    while (true) {
        struct dirent *ep = readdir(dir);
        if (ep == NULL) break;
        std::string name = std::string(ep->d_name);
        if (name != "." && name != "..") list.push_back(name);
    }
    closedir(dir);
    sort(list.begin(), list.end());
}

#else // _WIN32

// Windows implementation; see Unix implementation elsewhere in this file
bool Platform::filelib_fileExists(std::string filename) {
    return GetFileAttributesA(filename.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Windows implementation; see Unix implementation elsewhere in this file
bool Platform::filelib_isFile(std::string filename) {
    DWORD attr = GetFileAttributesA(filename.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_NORMAL);
}

// Windows implementation; see Unix implementation elsewhere in this file
bool Platform::filelib_isSymbolicLink(std::string filename) {
    DWORD attr = GetFileAttributesA(filename.c_str());
    return attr != INVALID_FILE_ATTRIBUTES
            && (attr & FILE_ATTRIBUTE_REPARSE_POINT);
}

// Windows implementation; see Unix implementation elsewhere in this file
bool Platform::filelib_isDirectory(std::string filename) {
    DWORD attr = GetFileAttributesA(filename.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Windows implementation; see Unix implementation elsewhere in this file
void Platform::filelib_setCurrentDirectory(std::string path) {
    if (!filelib_isDirectory(path) || !SetCurrentDirectoryA(path.c_str())) {
        error("setCurrentDirectory: Can't change to " + path);
    }
}

// Windows implementation; see Unix implementation elsewhere in this file
std::string Platform::filelib_getCurrentDirectory() {
    char path[MAX_PATH + 1];
    int n = GetCurrentDirectoryA(MAX_PATH + 1, path);
    return std::string(path, n);
}

// Windows implementation; see Unix implementation elsewhere in this file
std::string Platform::filelib_getTempDirectory() {
    char path[MAX_PATH + 1];
    int n = GetTempPathA(MAX_PATH + 1, path);
    return std::string(path, n);
}

// Windows implementation; see Unix implementation elsewhere in this file
void Platform::filelib_createDirectory(std::string path) {
    if (!CreateDirectoryA(path.c_str(), NULL)) {
        error("createDirectory: Can't create " + path);
    }
}

// Windows implementation; see Unix implementation elsewhere in this file
std::string Platform::filelib_getDirectoryPathSeparator() {
    return "\\";
}

// Windows implementation; see Unix implementation elsewhere in this file
std::string Platform::filelib_getSearchPathSeparator() {
    return ";";
}

// Windows implementation; see Unix implementation elsewhere in this file
std::string Platform::filelib_expandPathname(std::string filename) {
    if (filename == "") return "";
    int len = filename.length();
    for (int i = 0; i < len; i++) {
        if (filename[i] == '/') filename[i] = '\\';
    }
    return filename;
}

// Windows implementation; see Unix implementation elsewhere in this file
void Platform::filelib_listDirectory(std::string path, std::vector<std::string> & list) {
    if (path == "") path = ".";
    std::string pattern = path + "\\*.*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        error("listDirectory: Can't list directory");
    }
    list.clear();
    while (true) {
        std::string name = std::string(fd.cFileName);
        if (name != "." && name != "..") list.push_back(name);
        if (!FindNextFileA(h, &fd)) break;
    }
    FindClose(h);
    sort(list.begin(), list.end());
}

#endif // _WIN32

std::string Platform::os_getLastError() {
#ifdef _WIN32
    // Windows error-reporting code
    DWORD lastErrorCode = ::GetLastError();
    char* errorMsg = NULL;
    // Ask Windows to prepare a standard message for a GetLastError() code:
    ::FormatMessageA(
                   /* dwFlags */ FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   /* lpSource */ NULL,
                   /* dwMessageId */ lastErrorCode,
                   /* dwLanguageId */ LANG_NEUTRAL,
                   /* lpBuffer */ (LPSTR) &errorMsg,
                   /* dwSize */ 0,
                   /* arguments */ NULL);
    if (errorMsg) {
        return std::string(errorMsg);
    } else {
        return "";
    }
#else
    // Linux/Mac error-reporting code
    return std::string(strerror(errno));
#endif // _WIN32
}


Platform *getPlatform() {
    static Platform gp;
    return &gp;
}

