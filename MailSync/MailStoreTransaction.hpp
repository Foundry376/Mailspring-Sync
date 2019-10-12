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

#ifndef MailStoreTransaction_hpp
#define MailStoreTransaction_hpp

#include "MailStore.hpp"

using namespace std;

class MailStoreTransaction
{
public:
    /**
     * @brief Begins the SQLite transaction
     *
     * @param[in] store the MailStore
     *
     * Exception is thrown in case of error, then the Transaction is NOT initiated.
     */
    explicit MailStoreTransaction(MailStore * store, string nameHint = "");
    
    /**
     * @brief Safely rollback the transaction if it has not been committed.
     */
    virtual ~MailStoreTransaction() noexcept; // nothrow
    
    /**
     * @brief Commit the transaction.
     */
    void commit();
    
private:
    // Transaction must be non-copyable
    MailStoreTransaction(const MailStoreTransaction&);
    MailStoreTransaction& operator=(const MailStoreTransaction&);
    
private:
    MailStore*  mStore;     // < Reference to the SQLite Database Connection
    bool        mCommited;  // < True when commit has been called
    std::chrono::system_clock::time_point mStart;
    std::chrono::system_clock::time_point mBegan;
    string      mNameHint;
};

#endif
