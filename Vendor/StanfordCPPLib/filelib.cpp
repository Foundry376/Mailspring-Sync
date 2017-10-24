/*
 * File: filelib.cpp
 * -----------------
 * This file implements the filelib.h interface.  All platform dependencies
 * are managed through the platform interface.
 * 
 * @version 2015/07/05
 * - removed static global Platform variable, replaced by getPlatform as needed
 * - moved appendSpace function to simpio
 * @version 2015/04/12
 * - added promptUserForFile overload without stream parameter
 * @version 2014/10/19
 * - alphabetized function declarations
 * - converted many funcs to take const string& rather than string for efficiency
 * - added listDirectory overload that returns a Vector
 * @version 2014/10/08
 * - removed 'using namespace' statement
 */

#include "filelib.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "platform.h"
#include "error.h"
#include "simpio.h"
#include "strlib.h"

/* Prototypes */

static bool recursiveMatch(const std::string& str, int sx, const std::string& pattern, int px);
static void splitPath(const std::string& path, std::vector<std::string> & list);

/* Implementations */

void createDirectory(const std::string& path) {
    return getPlatform()->filelib_createDirectory(path);
}

void createDirectoryPath(const std::string& path) {
    size_t cp = 1;
    if (path == "") return;
    while ((cp = path.find('/', cp + 1)) != std::string::npos) {
        createDirectory(path.substr(0, cp - 1));
    }
    createDirectory(path);
}

std::string defaultExtension(const std::string& filename, const std::string& ext) {
    std::string extCopy = ext;
    bool force = (extCopy[0] == '*');
    if (force) extCopy = extCopy.substr(1);
    int dot = -1;
    size_t len = filename.length();
    for (int i = 0; i < len; i++) {
        char ch = filename[i];
        if (ch == '.') dot = i;
        if (ch == '/' || ch == '\\') dot = -1;
    }
    if (dot == -1) {
        force = true;
        dot = (int)len;
    }
    if (force) {
        return filename.substr(0, dot) + extCopy;
    } else {
        return filename;
    }
}

void deleteFile(const std::string& filename) {
    remove(expandPathname(filename).c_str());
}

std::string expandPathname(const std::string& filename) {
    return getPlatform()->filelib_expandPathname(filename);
}

bool fileExists(const std::string& filename) {
    return getPlatform()->filelib_fileExists(filename);
}

std::string findOnPath(const std::string& path, const std::string& filename) {
    std::ifstream stream;
    std::string result = openOnPath(stream, path, filename);
    if (result != "") stream.close();
    return result;
}

std::string getCurrentDirectory() {
    return getPlatform()->filelib_getCurrentDirectory();
}

std::string getDirectoryPathSeparator() {
    return getPlatform()->filelib_getDirectoryPathSeparator();
}

std::string getExtension(const std::string& filename) {
    int dot = -1;
    size_t len = filename.length();
    for (int i = 0; i < len; i++) {
        char ch = filename[i];
        if (ch == '.') dot = i;
        if (ch == '/' || ch == '\\') dot = -1;
    }
    if (dot == -1) {
        return "";
    } else {
        return filename.substr(dot);
    }
}

std::string getHead(const std::string& filename) {
    size_t slash = std::string::npos;
    size_t len = filename.length();
    for (size_t i = 0; i < len; i++) {
        char ch = filename[i];
        if (ch == '/' || ch == '\\') slash = i;
    }
    if (slash == std::string::npos) {
        return "";
    } else if (slash == 0) {
        return "/";
    } else {
        return filename.substr(0, slash);
    }
}

std::string getRoot(const std::string& filename) {
    int dot = -1;
    size_t len = filename.length();
    for (int i = 0; i < len; i++) {
        char ch = filename[i];
        if (ch == '.') dot = i;
        if (ch == '/' || ch == '\\') dot = -1;
    }
    if (dot == -1) {
        return filename;
    } else {
        return filename.substr(0, dot);
    }
}

std::string getSearchPathSeparator() {
    return getPlatform()->filelib_getSearchPathSeparator();
}

std::string getTail(const std::string& filename) {
    size_t slash = std::string::npos;
    size_t len = filename.length();
    for (size_t i = 0; i < len; i++) {
        char ch = filename[i];
        if (ch == '/' || ch == '\\') slash = i;
    }
    if (slash == std::string::npos) {
        return filename;
    } else {
        return filename.substr(slash + 1);
    }
}

std::string getTempDirectory() {
    return getPlatform()->filelib_getTempDirectory();
}

bool isDirectory(const std::string& filename) {
    return getPlatform()->filelib_isDirectory(filename);
}

bool isFile(const std::string& filename) {
    return getPlatform()->filelib_isFile(filename);
}

bool isSymbolicLink(const std::string& filename) {
    return getPlatform()->filelib_isSymbolicLink(filename);
}

void listDirectory(const std::string& path, std::vector<std::string>& list) {
    return getPlatform()->filelib_listDirectory(path, list);
}

bool matchFilenamePattern(const std::string& filename, const std::string& pattern) {
    return recursiveMatch(filename, 0, pattern, 0);
}

bool openFile(std::ifstream& stream, const std::string& filename) {
    stream.clear();
    stream.open(expandPathname(filename).c_str());
    return !stream.fail();
}

bool openFile(std::ofstream& stream, const std::string& filename) {
    stream.clear();
    stream.open(expandPathname(filename).c_str());
    return !stream.fail();
}

std::string openOnPath(std::ifstream& stream,
                       const std::string& path,
                       const std::string& filename) {
    std::vector<std::string> paths;
    splitPath(path, paths);
    for (std::string dir : paths) {
        std::string pathname = dir + "/" + filename;
        if (openFile(stream, pathname)) return pathname;
    }
    return "";
}

std::string openOnPath(std::ofstream& stream,
                       const std::string& path,
                       const std::string& filename) {
    std::vector<std::string> paths;
    splitPath(path, paths);
    for (std::string dir : paths) {
        std::string pathname = dir + "/" + filename;
        if (openFile(stream, pathname)) return pathname;
    }
    return "";
}

std::string promptUserForFile(std::ifstream& stream,
                              const std::string& prompt,
                              const std::string& reprompt) {
    std::string promptCopy = prompt;
    std::string repromptCopy = reprompt;
    if (reprompt == "") {
        repromptCopy = "Unable to open that file.  Try again.";
    }
    appendSpace(promptCopy);
    while (true) {
        std::cout << promptCopy;
        std::string filename;
        getline(std::cin, filename);
        if (!filename.empty()) {
            openFile(stream, filename);
            if (!stream.fail()) return filename;
            stream.clear();
        }
        std::cout << repromptCopy << std::endl;
        if (promptCopy == "") promptCopy = "Input file: ";
    }
}

std::string promptUserForFile(std::ofstream& stream,
                              const std::string& prompt,
                              const std::string& reprompt) {
    std::string promptCopy = prompt;
    std::string repromptCopy = reprompt;
    if (reprompt == "") {
        repromptCopy = "Unable to open that file.  Try again.";
    }
    appendSpace(promptCopy);
    while (true) {
        std::cout << promptCopy;
        std::string filename;
        getline(std::cin, filename);
        if (!filename.empty()) {
            openFile(stream, filename);
            if (!stream.fail()) return filename;
            stream.clear();
        }
        std::cout << repromptCopy << std::endl;
        if (promptCopy == "") promptCopy = "Output file: ";
    }
}

std::string promptUserForFile(const std::string& prompt,
                              const std::string& reprompt) {
    std::string promptCopy = prompt;
    std::string repromptCopy = reprompt;
    if (reprompt == "") {
        repromptCopy = "Unable to open that file.  Try again.";
    }
    appendSpace(promptCopy);
    while (true) {
        std::cout << promptCopy;
        std::string filename;
        getline(std::cin, filename);
        if (!filename.empty()) {
            std::ifstream stream;
            openFile(stream, filename);
            if (!stream.fail()) {
                stream.close();
                return filename;
            }
        }
        std::cout << repromptCopy << std::endl;
        if (promptCopy == "") promptCopy = "Input file: ";
    }
}

void readEntireFile(std::istream& is, std::vector<std::string>& lines) {
    lines.clear();
    while (true) {
        std::string line;
        getline(is, line);
        if (is.fail()) break;
        lines.push_back(line);
    }
}

std::string readEntireFile(const std::string& filename) {
    std::string out;
    if (readEntireFile(filename, out)) {
        return out;
    } else {
        error(std::string("input file not found or cannot be opened: ") + filename);
        return "";
    }
}

bool readEntireFile(const std::string& filename, std::string& out) {
    std::ifstream input;
    input.open(filename.c_str());
    if (input.fail()) {
        return false;
    }
    std::ostringstream output;
    while (true) {
        int ch = input.get();
        if (input.fail()) break;
        output << (char) ch;
    }
    input.close();
    out = output.str();
    return true;
}

void renameFile(const std::string& oldname, const std::string& newname) {
    std::string oldExpand = expandPathname(oldname);
    std::string newExpand = expandPathname(newname);
    rename(oldExpand.c_str(), newExpand.c_str());
}

void rewindStream(std::istream& input) {
    input.clear();                  // removes any current eof/failure flags
    input.seekg(0, std::ios::beg);  // tells the stream to seek back to the beginning
}

void setCurrentDirectory(const std::string& path) {
    return getPlatform()->filelib_setCurrentDirectory(path);
}

bool writeEntireFile(const std::string& filename,
                     const std::string& text,
                     bool append) {
    std::ofstream output;
    if (append) {
        output.open(filename.c_str(), std::ios_base::out | std::ios_base::app);
    } else {
        output.open(filename.c_str());
    }
    if (output.fail()) {
        return false;
    }
    output << text;
    output.close();
    return !output.fail();
}

/* Private functions */

static void splitPath(const std::string& path, std::vector<std::string> & list) {
    char sep = (path.find(';') == std::string::npos) ? ':' : ';';
    std::string pathCopy = path + sep;
    size_t start = 0;
    while (true) {
        size_t finish = pathCopy.find(sep, start);
        if (finish == std::string::npos) break;
        if (finish > start + 1) {
            list.push_back(pathCopy.substr(start, finish - start - 1));
        }
        start = finish + 1;
    }
}

static bool recursiveMatch(const std::string& str, int sx, const std::string& pattern, int px) {
    size_t slen = str.length();
    size_t plen = pattern.length();
    if (px == plen) return (sx == slen);
    char pch = pattern[px];
    if (pch == '*') {
        for (int i = sx; i <= slen; i++) {
            if (recursiveMatch(str, i, pattern, px + 1)) return true;
        }
        return false;
    }
    if (sx == slen) return false;
    char sch = str[sx];
    if (pch == '[') {
        bool match = false;
        bool invert = false;
        px++;
        if (px == plen) {
            error("matchFilenamePattern: missing ]");
        }
        if (pattern[px] == '^') {
            px++;
            invert = true;
        }
        while (px < plen && pattern[px] != ']') {
            if (px + 2 < plen && pattern[px + 1] == '-') {
                match |= (sch >= pattern[px] && sch <= pattern[px + 2]);
                px += 3;
            } else {
                match |= (sch == pattern[px]);
                px++;
            }
        }
        if (px == plen) {
            error("matchFilenamePattern: missing ]");
        }
        if (match == invert) return false;
    } else if (pch != '?') {
        if (pch != sch) return false;
    }
    return recursiveMatch(str, sx + 1, pattern, px + 1);
}
