//
//  MailStoreTransaction.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "MailStoreTransaction.hpp"

using namespace std;
using namespace std::chrono;

MailStoreTransaction::MailStoreTransaction(MailStore * store, string nameHint) :
    mStore(store), mCommited(false), mStart(system_clock::now()), mBegan(system_clock::now()), mNameHint(nameHint)
{
    mStore->beginTransaction();
    mBegan = system_clock::now();
}

MailStoreTransaction::~MailStoreTransaction() noexcept // nothrow
{
    if (false == mCommited) {
        try {
            mStore->rollbackTransaction();
        } catch (SQLite::Exception&) {
            // Never throw an exception in a destructor: error if
            // already rollbacked, but no harm is caused by this.
        }
    }
}

void MailStoreTransaction::commit()
{
    if (false == mCommited) {
        mStore->commitTransaction();
        mCommited = true;

        auto now = system_clock::now();
        auto elapsed = now - mStart;
        long long microseconds = duration_cast<std::chrono::microseconds>(elapsed).count();
        if (microseconds > 60 * 1000) { // 60ms
            long long waiting = duration_cast<std::chrono::microseconds>(mBegan - mStart).count();
            spdlog::get("logger")->warn("[SLOW] Transaction={} > 60ms ({} microseconds, {} waiting to aquire)", mNameHint, microseconds, waiting);
        }
        
    } else {
        throw SQLite::Exception("Transaction already commited.");
    }
}


