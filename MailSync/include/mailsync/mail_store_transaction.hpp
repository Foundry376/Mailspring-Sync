/** MailStoreTransaction [MailSync]
 *
 * Author(s): Ben Gotow
 */

/* LICENSE
* Copyright (C) 2017-2021 Foundry 376.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MailStoreTransaction_hpp
#define MailStoreTransaction_hpp

#include <string>

#include "mailsync/mail_store.hpp"

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
    explicit MailStoreTransaction(MailStore * store, std::string nameHint = "");

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
    MailStore* mStore;     // < Reference to the SQLite Database Connection
    bool mCommited;  // < True when commit has been called
    std::chrono::system_clock::time_point mStart;
    std::chrono::system_clock::time_point mBegan;
    std::string mNameHint;
};

#endif
