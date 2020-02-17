//
//  ThreadUtils.hpp
//  MailSync
//
//  Created by Ben Gotow on 7/8/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "ThreadUtils.h"
#include <spdlog/details/os.h>
#include <stdio.h>
#include <map>
#include <thread>

#ifdef _WIN32
#include <windows.h>
const DWORD MS_VC_EXCEPTION=0x406D1388;

#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
    DWORD dwType; // Must be 0x1000.
    LPCSTR szName; // Pointer to name (in user addr space).
    DWORD dwThreadID; // Thread ID (-1=caller thread).
    DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

static std::map<size_t, std::string> names{};

void SetThreadName(const char* threadName)
{
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = threadName;
    info.dwThreadID = GetCurrentThreadId();
    info.dwFlags = 0;
    
    names[spdlog::details::os::thread_id()] = threadName;

    __try
    {
        RaiseException( MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info );
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

#else

#include <unistd.h>
#include <mutex>
static std::map<size_t, std::string> names{};
static std::mutex namesMtx;

#ifdef _POSIX_THREADS
#include <pthread.h>

void SetThreadName(const char* threadName)
{
    namesMtx.lock();
    names[spdlog::details::os::thread_id()] = threadName;
    namesMtx.unlock();
#ifdef __APPLE__
    pthread_setname_np(threadName);
#else
    pthread_setname_np(pthread_self(), threadName);
#endif
}

#else

#include <sys/prctl.h>

void SetThreadName( const char* threadName)
{
    namesMtx.lock();
    names[spdlog::details::os::thread_id()] = threadName;
    namesMtx.unlock();
    prctl(PR_SET_NAME, threadName, 0, 0, 0);
}

#endif // pthread

#endif // win32

std::string * GetThreadName(size_t spdlog_thread_id) {
    return &names[spdlog_thread_id];
}
