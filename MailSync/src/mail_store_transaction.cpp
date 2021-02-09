#include "mailsync/mail_store_transaction.hpp"

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
        long long milliseconds = duration_cast<std::chrono::milliseconds>(elapsed).count();
        if (milliseconds > 80) { // 80ms
            long long waiting = duration_cast<std::chrono::milliseconds>(mBegan - mStart).count();
            spdlog::get("logger")->warn("[SLOW] Transaction={} > 80ms ({}ms, {} waiting to aquire)", mNameHint, milliseconds, waiting);
        }

    } else {
        throw SQLite::Exception("Transaction already commited.");
    }
}
