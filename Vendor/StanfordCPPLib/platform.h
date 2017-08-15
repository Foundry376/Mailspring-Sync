/*
 * File: platform.h
 * ----------------
 * This file defines the <code>Platform</code> class, which encapsulates
 * the platform-specific parts of the StanfordCPPLib package.  This file is
 * logically part of the implementation and is not interesting to clients.
 *
 * @version 2015/11/07
 * - added GTable back-end methods
 * @version 2014/11/20
 * - added gwindow clearCanvas method
 * @version 2014/11/15
 * - improvements to autograder unit test GUI
 * @version 2014/11/14
 * - added method to set unit test runtime in MS
 * @version 2014/10/31
 * - added functions for graphical autograder unit tests
 * - added setVisible for autograder input panel
 * @version 2014/10/08
 * - removed dependency on using namespace statement
 * - filelib_getTempDirectory
 * - gbufferedimage_constructor, gbufferedimage_resize fixed
 * - GOptionPane functions implemented
 * - url_download implemented
 */

#ifndef _platform_h
#define _platform_h

#include <string>
#include <vector>

class Platform {
private:
    Platform();
    friend Platform *getPlatform();

public:
    virtual ~Platform();
    std::string cpplib_getCppLibraryVersion();
    std::string cpplib_getJavaBackEndVersion();
    void cpplib_setCppLibraryVersion();
    std::string file_openFileDialog(std::string title, std::string mode, std::string path);
    void filelib_createDirectory(std::string path);
    std::string filelib_expandPathname(std::string filename);
    bool filelib_fileExists(std::string filename);
    std::string filelib_getCurrentDirectory();
    std::string filelib_getDirectoryPathSeparator();
    std::string filelib_getSearchPathSeparator();
    std::string filelib_getTempDirectory();
    bool filelib_isDirectory(std::string filename);
    bool filelib_isFile(std::string filename);
    bool filelib_isSymbolicLink(std::string filename);
    void filelib_listDirectory(std::string path, std::vector<std::string>& list);
    void filelib_setCurrentDirectory(std::string path);

    std::string os_getLastError();
    bool regex_match(std::string s, std::string regexp);
    int regex_matchCount(std::string s, std::string regexp);
    int regex_matchCountWithLines(std::string s, std::string regexp, std::string& linesOut);
    std::string regex_replace(std::string s, std::string regexp, std::string replacement, int limit = -1);
    void setStackSize(unsigned int stackSize);
    int url_download(std::string url, std::string filename);
};

Platform *getPlatform();

#endif
