/*
 * File: call_stack_windows.cpp
 * ----------------------------
 * Windows implementation of the call_stack class.
 *
 * @author Marty Stepp
 * @version 2019/04/03
 * - fixed compiler errors for 64-bit Windows MinGW compiler (context struct)
 * @version 2018/10/22
 * - bug fix for STL vector vs Stanford Vector
 * @version 2018/09/12
 * - fixed compiler errors with os_getLastError and other misc warnings
 * @version 2017/10/24
 * - removed SYMOPT_DEBUG from SymSetOptions to avoid spurious console output
 * @version 2017/10/20
 * - changed null/0s to nullptr to remove compiler warnings
 * @version 2016/10/04
 * - removed all static variables (replaced with STATIC_VARIABLE macros)
 * @version 2015/07/05
 * - removed static global Platform variable, replaced by getPlatform as needed
 * @version 2015/05/28
 */

#ifdef _WIN32
#include "call_stack.h"
#include "dbg.h"

#include <iostream>

namespace stacktrace {
	
	call_stack::call_stack(const size_t /*num_discard = 0*/) {
		auto a = dbg::stack_trace();
			for (auto line: a) {
				entry e;
				e.file = line.file;
				e.line = line.line;
				e.lineStr = line.line > 0 ? std::to_string(line.line) : "";
				e.function = line.name;
				e.address = (void*)line.address;

				stack.push_back(e);
			}
	}
	call_stack::~call_stack() throw() {
		// automatic cleanup
	}

}// namespace stacktrace

#endif // _WIN32
