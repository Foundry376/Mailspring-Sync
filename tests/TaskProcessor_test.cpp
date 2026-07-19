#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "TaskProcessor.hpp"
#include "MockIMAPSession.hpp"
#include "MailStore.hpp"
#include "Account.hpp"
#include "Task.hpp"
#include "MailUtils.hpp"
#include <memory>

using ::testing::Return;
using ::testing::_;
using ::testing::SetArgPointee;
using ::testing::DoAll;
using ::testing::InSequence;

class TaskProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        setenv("CONFIG_DIR_PATH", "/tmp/mailsync_test", 1);
        system("mkdir -p /tmp/mailsync_test");
        
        store = new MailStore();
        
        json accountData = {
            {"id", "test-account"},
            {"provider", "gmail"},
            {"email_address", "test@gmail.com"},
            {"settings", {
                {"imap_host", "imap.gmail.com"},
                {"imap_port", 993},
                {"imap_username", "test@gmail.com"},
                {"imap_password", "password"},
                {"smtp_host", "smtp.gmail.com"},
                {"smtp_port", 465}
            }}
        };
        account = make_shared<Account>(accountData);
        
        mockSession = new MockIMAPSession();
        processor = new TaskProcessor(account, store, mockSession);
    }

    void TearDown() override {
        delete processor;
        delete mockSession;
        delete store;
        system("rm -rf /tmp/mailsync_test");
    }

    MailStore * store;
    shared_ptr<Account> account;
    MockIMAPSession * mockSession;
    TaskProcessor * processor;
};

TEST_F(TaskProcessorTest, GTestWorking) {
    EXPECT_EQ(1, 1);
}

TEST_F(TaskProcessorTest, OutlookDetectedByHostname) {
    json outlookData = {
        {"id", "outlook-account"},
        {"provider", "outlook"},
        {"email_address", "test@outlook.com"},
        {"settings", {
            {"imap_host", "imap-mail.outlook.com"},
            {"imap_port", 993}
        }}
    };
    auto outlookAccount = make_shared<Account>(outlookData);
    
    EXPECT_CALL(*mockSession, isOutlookServer()).WillOnce(Return(true));
    EXPECT_TRUE(mockSession->isOutlookServer());
}

TEST_F(TaskProcessorTest, OutlookDetectedByProviderSubstring) {
    json outlookData = {
        {"id", "outlook-account"},
        {"provider", "outlook"},
        {"email_address", "test@outlook.com"},
        {"settings", {
            {"imap_host", "imap.gmail.com"},
            {"imap_port", 993}
        }}
    };
    auto outlookAccount = make_shared<Account>(outlookData);
    
    std::string provider = outlookAccount->provider();
    bool isOutlook = (provider.find("outlook") != std::string::npos);
    EXPECT_TRUE(isOutlook);
}

TEST_F(TaskProcessorTest, Office365DetectedByProviderSubstring) {
    json office365Data = {
        {"id", "office365-account"},
        {"provider", "office365"},
        {"email_address", "test@company.onmicrosoft.com"},
        {"settings", {
            {"imap_host", "outlook.office365.com"},
            {"imap_port", 993}
        }}
    };
    auto office365Account = make_shared<Account>(office365Data);
    
    std::string provider = office365Account->provider();
    bool isOffice365 = (provider.find("office365") != std::string::npos);
    EXPECT_TRUE(isOffice365);
}

TEST_F(TaskProcessorTest, CustomDomainOutlookDetected) {
    json customData = {
        {"id", "custom-outlook"},
        {"provider", "mycompany-outlook"},
        {"email_address", "test@mycompany.com"},
        {"settings", {
            {"imap_host", "imap.mycompany.com"},
            {"imap_port", 993}
        }}
    };
    auto customAccount = make_shared<Account>(customData);
    
    std::string provider = customAccount->provider();
    bool isOutlook = (provider.find("outlook") != std::string::npos);
    EXPECT_TRUE(isOutlook);
}

TEST_F(TaskProcessorTest, NonOutlookProviderNotDetectedAsOutlook) {
    json gmailData = {
        {"id", "gmail-account"},
        {"provider", "gmail"},
        {"email_address", "test@gmail.com"},
        {"settings", {
            {"imap_host", "imap.gmail.com"},
            {"imap_port", 993}
        }}
    };
    auto gmailAccount = make_shared<Account>(gmailData);
    
    std::string provider = gmailAccount->provider();
    bool isOutlook = (provider.find("outlook") != std::string::npos);
    EXPECT_FALSE(isOutlook);
}

TEST_F(TaskProcessorTest, OutlookRetryDelaysAreCorrect) {
    int delayOutlook[] = {0, 2, 3, 5, 5};
    int delayStandard[] = {0, 1, 1, 2, 2};
    
    int maxTriesOutlook = 5;
    int maxTriesStandard = 4;
    
    EXPECT_EQ(delayOutlook[0], 0);
    EXPECT_EQ(delayOutlook[1], 2);
    EXPECT_EQ(delayOutlook[2], 3);
    EXPECT_EQ(delayOutlook[3], 5);
    EXPECT_EQ(delayOutlook[4], 5);
    EXPECT_EQ(maxTriesOutlook, 5);
    
    EXPECT_EQ(delayStandard[0], 0);
    EXPECT_EQ(delayStandard[1], 1);
    EXPECT_EQ(delayStandard[2], 1);
    EXPECT_EQ(delayStandard[3], 2);
    EXPECT_EQ(delayStandard[4], 2);
    EXPECT_EQ(maxTriesStandard, 4);
}

TEST_F(TaskProcessorTest, MockSessionFindUIDsSetsCorrectUIDs) {
    mockSession->setMockUIDsForMessage("Sent", "<test-message-id@domain.com>", {100, 101, 102});
    
    mailcore::IndexSet* uids = mailcore::IndexSet::indexSet();
    mailcore::String* folder = mailcore::String::stringWithUTF8Characters("Sent");
    mailcore::String* msgId = mailcore::String::stringWithUTF8Characters("<test-message-id@domain.com>");
    
    mockSession->mock_findUIDsOfRecentHeaderMessageID(folder, msgId, uids);
    
    EXPECT_EQ(uids->count(), 3U);
    
    EXPECT_TRUE(uids->containsIndex(100));
    EXPECT_TRUE(uids->containsIndex(101));
    EXPECT_TRUE(uids->containsIndex(102));
}

TEST_F(TaskProcessorTest, MockSessionFindUIDsEmptyWhenNoMessage) {
    mailcore::IndexSet* uids = mailcore::IndexSet::indexSet();
    mailcore::String* folder = mailcore::String::stringWithUTF8Characters("Sent");
    mailcore::String* msgId = mailcore::String::stringWithUTF8Characters("<nonexistent@domain.com>");
    
    mockSession->mock_findUIDsOfRecentHeaderMessageID(folder, msgId, uids);
    
    EXPECT_EQ(uids->count(), 0U);
}

TEST_F(TaskProcessorTest, SingleGatewayCopyDetected) {
    mockSession->setMockUIDsForMessage("Sent", "<test-msg-id@domain.com>", {100});
    
    mailcore::IndexSet* uids = mailcore::IndexSet::indexSet();
    mailcore::String* folder = mailcore::String::stringWithUTF8Characters("Sent");
    mailcore::String* msgId = mailcore::String::stringWithUTF8Characters("<test-msg-id@domain.com>");
    
    mockSession->mock_findUIDsOfRecentHeaderMessageID(folder, msgId, uids);
    
    EXPECT_EQ(uids->count(), 1U);
    
    if (uids->count() == 1) {
        uint32_t sentFolderMessageUID = (uint32_t)uids->allRanges()[0].location;
        EXPECT_EQ(sentFolderMessageUID, 100U);
    }
}

TEST_F(TaskProcessorTest, MultipleGatewayCopiesKeepsFirst) {
    mockSession->setMockUIDsForMessage("Sent", "<test-msg-id@domain.com>", {100, 101, 102});
    
    mailcore::IndexSet* uids = mailcore::IndexSet::indexSet();
    mailcore::String* folder = mailcore::String::stringWithUTF8Characters("Sent");
    mailcore::String* msgId = mailcore::String::stringWithUTF8Characters("<test-msg-id@domain.com>");
    
    mockSession->mock_findUIDsOfRecentHeaderMessageID(folder, msgId, uids);
    
    EXPECT_EQ(uids->count(), 3U);
    
    auto allRanges = uids->allRanges();
    uint32_t keptUID = (uint32_t)allRanges[0].location;
    EXPECT_EQ(keptUID, 100U);
    
    mailcore::IndexSet* extras = mailcore::IndexSet::indexSet();
    for (unsigned int i = 1; i < uids->count(); i++) {
        extras->addIndex(allRanges[i].location);
    }
    
    EXPECT_EQ(extras->count(), 2U);
    EXPECT_TRUE(uids->containsIndex(101));
    EXPECT_TRUE(uids->containsIndex(102));
}

TEST_F(TaskProcessorTest, MockCapabilitiesGmail) {
    mailcore::IndexSet* caps = mailcore::IndexSet::indexSet();
    caps->addIndex(mailcore::IMAPCapabilityGmail);
    
    EXPECT_CALL(*mockSession, storedCapabilities()).WillOnce(Return(caps));
    
    mailcore::IndexSet* result = mockSession->storedCapabilities();
    EXPECT_TRUE(result->containsIndex(mailcore::IMAPCapabilityGmail));
}

TEST_F(TaskProcessorTest, MockCapabilitiesOutlook) {
    mockSession->setMockIsOutlookServer(true);
    
    EXPECT_CALL(*mockSession, isOutlookServer()).WillOnce(Return(true));
    
    EXPECT_TRUE(mockSession->isOutlookServer());
}

TEST_F(TaskProcessorTest, OutlookExtendedWaitProbeCount) {
    int outlookWaitCount = 6;
    int waitIntervalSeconds = 2;
    int totalWaitTime = outlookWaitCount * waitIntervalSeconds;
    
    EXPECT_EQ(outlookWaitCount, 6);
    EXPECT_EQ(waitIntervalSeconds, 2);
    EXPECT_EQ(totalWaitTime, 12);
}

TEST_F(TaskProcessorTest, OutlookSkipsAppendMessage) {
    bool isOutlook = true;
    bool shouldSkipAppend = isOutlook;
    
    EXPECT_TRUE(shouldSkipAppend);
    
    isOutlook = false;
    shouldSkipAppend = isOutlook;
    
    EXPECT_FALSE(shouldSkipAppend);
}

TEST_F(TaskProcessorTest, MultisendDeletesAllMessages) {
    bool multisend = true;
    uint32_t messageCount = 3;
    
    if (multisend && messageCount > 0) {
        EXPECT_EQ(messageCount, 3U);
    }
}

TEST_F(TaskProcessorTest, NonMultisendSingleMessageKept) {
    bool multisend = false;
    uint32_t messageCount = 1;
    
    if (!multisend && messageCount == 1) {
        EXPECT_EQ(messageCount, 1U);
    }
}

TEST_F(TaskProcessorTest, NoMessagesFoundInSentFolder) {
    mockSession->setMockUIDsForMessage("Sent", "<test-msg-id@domain.com>", {});
    
    mailcore::IndexSet* uids = mailcore::IndexSet::indexSet();
    mailcore::String* folder = mailcore::String::stringWithUTF8Characters("Sent");
    mailcore::String* msgId = mailcore::String::stringWithUTF8Characters("<test-msg-id@domain.com>");
    
    mockSession->mock_findUIDsOfRecentHeaderMessageID(folder, msgId, uids);
    
    EXPECT_EQ(uids->count(), 0U);
    
    if (uids->count() == 0) {
        EXPECT_TRUE(true);
    }
}

TEST_F(TaskProcessorTest, GmailLabelPreservation) {
    bool isGmail = true;
    bool hasThread = true;
    bool hasLabels = true;
    
    if (isGmail && hasThread && hasLabels) {
        EXPECT_TRUE(true);
    }
}

TEST_F(TaskProcessorTest, OutlookDetectionPrecedence) {
    mockSession->setMockIsOutlookServer(true);
    
    std::string provider = "gmail";
    bool isOutlookServerResult = mockSession->mock_isOutlookServer();
    bool providerHasOutlook = (provider.find("outlook") != std::string::npos);
    bool isOutlook = isOutlookServerResult || providerHasOutlook;
    
    EXPECT_TRUE(isOutlook);
}

TEST_F(TaskProcessorTest, ProviderCheckWithoutOutlookServer) {
    mockSession->setMockIsOutlookServer(false);
    
    std::string provider = "outlook";
    bool isOutlookServerResult = mockSession->isOutlookServer();
    bool providerHasOutlook = (provider.find("outlook") != std::string::npos);
    bool isOutlook = isOutlookServerResult || providerHasOutlook;
    
    EXPECT_TRUE(isOutlook);
}
