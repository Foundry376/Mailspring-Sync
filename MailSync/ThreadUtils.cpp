//
//  ThreadUtils.hpp
//  MailSync
//
//  Created by Ben Gotow on 7/8/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include <stdio.h>
#include "ThreadUtils.h"

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


void SetThreadName(const char* threadName)
{
    
    // DWORD dwThreadID = ::GetThreadId( static_cast<HANDLE>( t.native_handle() ) );
    
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = threadName;
    info.dwThreadID = GetCurrentThreadId();
    info.dwFlags = 0;
    
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
#ifdef _POSIX_THREADS

#include <pthread.h>
void SetThreadName(const char* threadName)
{
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
    prctl(PR_SET_NAME, threadName, 0, 0, 0);
}

#endif // pthread

#endif // win32
