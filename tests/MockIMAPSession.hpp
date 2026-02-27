#ifndef MOCKIMAPSESSION_HPP
#define MOCKIMAPSESSION_HPP

#include <gmock/gmock.h>
#include <MailCore/MailCore.h>
#include <map>
#include <string>

class MockIMAPSession : public mailcore::IMAPSession {
public:
    MockIMAPSession() : 
        _isOutlookServer(false),
        _nextUID(1) {}
    
    MOCK_METHOD(void, appendMessage, (mailcore::String * folder, mailcore::Data * messageData, mailcore::MessageFlag flags, mailcore::IMAPProgressCallback * progressCallback, uint32_t * createdUID, mailcore::ErrorCode * pError), (override));
    
    MOCK_METHOD(void, findUIDsOfRecentHeaderMessageID, (mailcore::String * folder, mailcore::String * headerMessageID, mailcore::IndexSet * uids), (override));
    
    MOCK_METHOD(bool, isOutlookServer, (), (override));
    
    MOCK_METHOD(mailcore::IndexSet *, storedCapabilities, (), (override));
    
    MOCK_METHOD(void, select, (mailcore::String * folder, mailcore::ErrorCode * pError), (override));
    
    MOCK_METHOD(void, storeFlagsByUID, (mailcore::String * folder, mailcore::IndexSet * uids, mailcore::IMAPStoreFlagsRequestKind kind, mailcore::MessageFlag flags, mailcore::ErrorCode * pError), (override));
    
    MOCK_METHOD(void, moveMessages, (mailcore::String * folder, mailcore::IndexSet * uidSet, mailcore::String * destFolder, mailcore::HashMap ** pUidMapping, mailcore::ErrorCode * pError), (override));
    
    MOCK_METHOD(void, expungeUIDs, (mailcore::String * folder, mailcore::IndexSet * uidSet, mailcore::ErrorCode * pError), (override));
    
    MOCK_METHOD(mailcore::Array *, fetchMessagesByUID, (mailcore::String * folder, mailcore::IMAPMessagesRequestKind requestKind, mailcore::IndexSet * uids, mailcore::IMAPProgressCallback * progressCallback, mailcore::ErrorCode * pError), (override));
    
    MOCK_METHOD(void, storeLabelsByUID, (mailcore::String * folder, mailcore::IndexSet * uids, mailcore::IMAPStoreFlagsRequestKind kind, mailcore::Array * labels, mailcore::ErrorCode * pError), (override));

    bool mock_isOutlookServer() const { return _isOutlookServer; }
    void setMockIsOutlookServer(bool value) { _isOutlookServer = value; }
    
    void mock_findUIDsOfRecentHeaderMessageID(mailcore::String * folder, mailcore::String * headerMessageID, mailcore::IndexSet * uids) {
        std::string key = std::string(folder->UTF8Characters()) + ":" + 
                         std::string(headerMessageID->UTF8Characters());
        auto it = _mockUIDs.find(key);
        if (it != _mockUIDs.end()) {
            for (uint32_t uid : it->second) {
                uids->addIndex(uid);
            }
        }
    }
    
    void setMockUIDsForMessage(const std::string& folder, const std::string& msgId, const std::vector<uint32_t>& uids) {
        std::string key = folder + ":" + msgId;
        _mockUIDs[key] = uids;
    }
    
    void clearMockUIDs() {
        _mockUIDs.clear();
    }
    
    uint32_t getAndIncrementNextUID() {
        return _nextUID++;
    }
    
private:
    bool _isOutlookServer;
    uint32_t _nextUID;
    std::map<std::string, std::vector<uint32_t>> _mockUIDs;
};

#endif // MOCKIMAPSESSION_HPP
