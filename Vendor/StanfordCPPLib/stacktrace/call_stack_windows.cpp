/*
 * File: base64.h
 * --------------
 * Windows implementation of the call_stack class.
 *
 * @author Marty Stepp
 * @version 2015/07/05
 * - removed static global Platform variable, replaced by getPlatform as needed
 * @version 2015/05/28
 */

#ifdef _WIN32
#include "call_stack.h"
#include <windows.h>
#  undef MOUSE_EVENT
#  undef KEY_EVENT
#  undef MOUSE_MOVED
#  undef HELP_KEY
#include <tchar.h>
#include <assert.h>
#include <errno.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string>
#include <windows.h>
#include <tchar.h>
#define _NO_CVCONST_H
// #include <dbghelp.h>
#include <imagehlp.h>
#include "error.h"
#include "exceptions.h"
#include "platform.h"
#include "strlib.h"
#ifndef _MSC_VER
#include <cxxabi.h>
#endif

namespace stacktrace {

const int WIN_STACK_FRAMES_TO_SKIP = 0;
const int WIN_STACK_FRAMES_MAX = 20;

// line = "ZNK6VectorIiE3getEi at vector.h:587"
//         <FUNCTION> at <LINESTR>
void injectAddr2lineInfo(entry& ent, const std::string& line) {
    ent.line     = 0;
    int atIndex = stringIndexOf(line, " at ");
    if (atIndex >= 0) {
        if (ent.function.empty()) {
            ent.function = line.substr(0, atIndex);
        }
        ent.lineStr  = line.substr(atIndex + 4);

        int colonIndex = stringIndexOf(ent.lineStr, ":");
        if (colonIndex >= 0) {
            ent.file = ent.lineStr.substr(0, colonIndex);
            std::string rest = ent.lineStr.substr(colonIndex + 1);
            if (stringIsInteger(rest)) {
                ent.line = stringToInteger(rest);
            }
        }
    } else {
        if (ent.function.empty()) {
            ent.function = line;
        }
    }

    // demangle function name if necessary
    if (startsWith(ent.function, "Z")) {
        ent.function = "_" + ent.function;
    }

#ifndef _MSC_VER
    if (startsWith(ent.function, "_Z")) {
        int status;
        char* demangled = abi::__cxa_demangle(ent.function.c_str(), NULL, 0, &status);
        if (status == 0 && demangled) {
            ent.function = demangled;
        }
    }
#endif
}

call_stack::call_stack(const size_t /*num_discard = 0*/) {
    // getting a stack trace on Windows / MinGW is loads of fun (not)
    std::vector<void*> traceVector;
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    void* fakeStackPtr = stacktrace::getFakeCallStackPointer();
    if (fakeStackPtr) {
        // set up fake stack for partial trace
        LPEXCEPTION_POINTERS exceptionInfo = (LPEXCEPTION_POINTERS) fakeStackPtr;
        if (exceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
            // can't do stack walking in Windows when a stack overflow happens :-/
            traceVector.push_back((void*) exceptionInfo->ContextRecord->Eip);
        } else {
            SymInitialize(GetCurrentProcess(), 0, TRUE);
            STACKFRAME frame = {0};
            frame.AddrPC.Offset    = exceptionInfo->ContextRecord->Eip;
            frame.AddrPC.Mode      = AddrModeFlat;
            frame.AddrStack.Offset = exceptionInfo->ContextRecord->Esp;
            frame.AddrStack.Mode   = AddrModeFlat;
            frame.AddrFrame.Offset = exceptionInfo->ContextRecord->Ebp;
            frame.AddrFrame.Mode   = AddrModeFlat;
            while ((int) traceVector.size() < WIN_STACK_FRAMES_MAX &&
                   StackWalk(IMAGE_FILE_MACHINE_I386,
                             process,
                             thread,
                             &frame,
                             exceptionInfo->ContextRecord,
                             0,
                             SymFunctionTableAccess,
                             SymGetModuleBase,
                             0)) {
                traceVector.push_back((void*) frame.AddrPC.Offset);
            }
        }
    } else {
        if (!::SymSetOptions(
                             // ::SymGetOptions()
                               SYMOPT_DEBUG
                             | SYMOPT_DEFERRED_LOADS
                             | SYMOPT_INCLUDE_32BIT_MODULES
                             // | SYMOPT_UNDNAME
                             | SYMOPT_CASE_INSENSITIVE
                             | SYMOPT_LOAD_LINES)) {
            // std::cout << "SymSetOptions failed!" << std::endl;
            // return;
        }
        if (!::SymInitialize(
                /* process */ process,
                /* user-defined search path */ NULL,
                /* include current process */ TRUE)) {
            // std::cout << "SymInitialize failed!" << std::endl;
            // return;
        }

        void* trace[WIN_STACK_FRAMES_MAX];
        USHORT frameCount = ::CaptureStackBackTrace(
                    /* framesToSkip */ WIN_STACK_FRAMES_TO_SKIP,
                    /* framesToCapture; must be < 63 */ WIN_STACK_FRAMES_MAX,
                    trace,
                    /* hash */ NULL
                    );
        for (int i = 0; i < frameCount; i++) {
            traceVector.push_back(trace[i]);
        }

        // try to load module symbol information; this always fails for me  :-/
        DWORD64 BaseAddr = 0;
        DWORD   FileSize = 0;
        const char* progFileC = exceptions::getProgramNameForStackTrace().c_str();
        char* progFile = (char*) progFileC;
        if (!::SymLoadModule(
                process,      // Process handle of the current process
                NULL,         // Handle to the module's image file (not needed)
                progFile,     // Path/name of the file
                NULL,         // User-defined short name of the module (it can be NULL)
                BaseAddr,     // Base address of the module (cannot be NULL if .PDB file is used, otherwise it can be NULL)
                FileSize)) {      // Size of the file (cannot be NULL if .PDB file is used, otherwise it can be NULL)
            // std::cout << "Error: SymLoadModule() failed: " << getPlatform()->os_getLastError() << std::endl;
            // return;
        }
    }

    // let's also try to get the line numbers via an external command-line process 'addr2line'
    // (ought to be able to get this information through C function 'backtrace', but for some
    // reason, Qt Creator's shipped version of MinGW does not include this functionality, argh)
    std::string addr2lineOutput;
    std::vector<std::string> addr2lineLines;
    if (!traceVector.empty()) {
        int result = addr2line_all(traceVector, addr2lineOutput);
        if (result == 0) {
            addr2lineLines = stringSplit(addr2lineOutput, "\n");
        }
    }

    SYMBOL_INFO* symbol = (SYMBOL_INFO*) calloc(sizeof(SYMBOL_INFO) + 1024 * sizeof(char), 1);
    symbol->MaxNameLen   = 1020;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    for (int i = 0; i < (int) traceVector.size(); ++i) {
        entry ent;
        ent.address = traceVector[i];
        if (process && ::SymFromAddr(process, (DWORD64) traceVector[i], 0, symbol)) {
            ent.function = symbol->Name;
        }
        // internal stuff failed, so load from external process
        if (i < addr2lineLines.size()) {
            injectAddr2lineInfo(ent, addr2line_clean(addr2lineLines[i]));
        } else {
            injectAddr2lineInfo(ent, "");
        }

        if (!ent.function.empty() || ent.line > 0) {
            stack.push_back(ent);
        }
    }
    free(symbol);
}

call_stack::~call_stack() throw() {
    // automatic cleanup
}

// BG ADDED BELOW

/*
* Run a sub-process and capture its output.
*/
int execAndCapture(std::string cmd, std::string& output) {
#ifdef _WIN32
	// Windows code for external process (ugly)
	HANDLE g_hChildStd_IN_Rd = NULL;
	HANDLE g_hChildStd_IN_Wr = NULL;
	HANDLE g_hChildStd_OUT_Rd = NULL;
	HANDLE g_hChildStd_OUT_Wr = NULL;
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;
	if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0)) {
		return 1;   // fail
	}
	if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) {
		return 1;   // fail
	}
	if (!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0)) {
		return 1;   // fail
	}
	if (!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0)) {
		return 1;   // fail
	}

	// CreateChildProcess();
	PROCESS_INFORMATION piProcInfo;
	STARTUPINFOA siStartInfo;
	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdError = g_hChildStd_OUT_Wr;
	siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
	siStartInfo.hStdInput = g_hChildStd_IN_Rd;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	if (!CreateProcessA(
		NULL,
		(char*)cmd.c_str(),   // command line
		NULL,                  // process security attributes
		NULL,                  // primary thread security attributes
		TRUE,                  // handles are inherited
		CREATE_NO_WINDOW,      // creation flags
		NULL,                  // use parent's environment
		NULL,                  // use parent's current directory
		&siStartInfo,          // STARTUPINFO pointer
		&piProcInfo)) {        // receives PROCESS_INFORMATION
		std::cerr << "CREATE PROCESS FAIL: " << std::endl;
		std::cerr << cmd << std::endl;
		return 1;   // fail
	}

	// close the subprocess's handles (waits for it to finish)
	WaitForSingleObject(piProcInfo.hProcess, INFINITE);
	CloseHandle(piProcInfo.hProcess);
	CloseHandle(piProcInfo.hThread);

	// ReadFromPipe();
	DWORD dwRead;
	const int BUFSIZE = 65536;
	CHAR chBuf[BUFSIZE] = { 0 };
	if (!ReadFile(g_hChildStd_OUT_Rd, chBuf, BUFSIZE, &dwRead, NULL) || dwRead == 0) {
		return 1;
	}
	std::ostringstream out;
	for (int i = 0; i < (int)dwRead; i++) {
		out.put(chBuf[i]);
	}

	output = out.str();
	return 0;
#else
	// Linux / Mac code for external process
	cmd += " 2>&1";
	FILE* pipe = popen(cmd.c_str(), "r");
	if (!pipe) {
		return -1;
	}
	char buffer[65536] = { 0 };
	output = "";
	while (!feof(pipe)) {
		if (fgets(buffer, 65536, pipe) != NULL) {
			output += buffer;
		}
	}
	return pclose(pipe);
#endif // _WIN32
}



static void* fakeCallStackPointer = NULL;


void* getFakeCallStackPointer() {
	return fakeCallStackPointer;
}

void setFakeCallStackPointer(void* ptr) {
	fakeCallStackPointer = ptr;
}

std::string addr2line_clean(std::string line) {
#if defined(_WIN32)
	// TODO: implement on Windows
	// "ZN10stacktrace25print_stack_trace_windowsEv at C:\Users\stepp\Documents\StanfordCPPLib\build\stanfordcpplib-windows-Desktop_Qt_5_3_MinGW_32bit-Debug/../../StanfordCPPLib/stacktrace/call_stack_windows.cpp:126"
#elif defined(__APPLE__)
	// Mac OS X version (atos)
	// "Vector<int>::checkIndex(int) const (in Autograder_QtCreatorProject) (vector.h:764)"
	if (line.find(" (") != std::string::npos) {
		line = line.substr(line.rfind(" (") + 2);
	}
	if (line.find(')') != std::string::npos) {
		line = line.substr(0, line.rfind(')'));
	}
	line = trim(line);
#elif defined(__GNUC__)
	// Linux version (addr2line)
	// "_Z4Mainv at /home/stepp/.../FooProject/src/mainfunc.cpp:131"
	if (line.find(" at ") != std::string::npos) {
		line = line.substr(line.rfind(" at ") + 4);
	}
	if (line.find('/') != std::string::npos) {
		line = line.substr(line.rfind('/') + 1);
	}

	// strip extra parenthesized info from the end
	if (line.find(" (") != std::string::npos) {
		line = line.substr(0, line.rfind(" ("));
	}
	line = trim(line);
#endif
	return line;
}


int addr2line_all(std::vector<void*> addrsVector, std::string& output) {
	int length = (int)addrsVector.size();
	// turn the addresses into a space-separated string
	std::ostringstream out;
	for (int i = 0; i < length; i++) {
		out << " " << std::hex << std::setfill('0') << addrsVector[i];
	}
	std::string addrsStr = out.str();
	out.str("");

	// have addr2line map the address to the relent line in the code
#if defined(__APPLE__)
	// Mac OS X
	out << "atos -o " << exceptions::getProgramNameForStackTrace() << addrsStr;
#elif defined(_WIN32)
	// Windows
	out << "addr2line.exe -f -i -C -s -p -e \"" << exceptions::getProgramNameForStackTrace() << "\"" << addrsStr;
#else
	// Linux
	out << "addr2line -f -i -C -s -p -e " << exceptions::getProgramNameForStackTrace() << addrsStr;
#endif
	std::string command = out.str();
	int result = execAndCapture(command, output);
	return result;
}

} // namespace stacktrace

#endif // _WIN32
