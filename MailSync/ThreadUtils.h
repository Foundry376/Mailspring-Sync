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

#ifndef ThreadUtils_hpp
#define ThreadUtils_hpp

#include <string>

std::string* GetThreadName(size_t spdlog_thread_id);
void SetThreadName(const char* threadName);

#endif /* ThreadUtils_hpp */
