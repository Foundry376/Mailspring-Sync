#ifndef MAILCORE_MCSMTPSESSION_H

#define MAILCORE_MCSMTPSESSION_H

#include <MailCore/MCBaseTypes.h>
#include <MailCore/MCMessageConstants.h>

#ifdef __cplusplus

namespace mailcore {
    
    class Address;
    class SMTPProgressCallback;
    class MessageBuilder;
    
    class MAILCORE_EXPORT SMTPSession : public Object {
    public:
        SMTPSession();
        virtual ~SMTPSession();
        
        virtual void setHostname(String * hostname);
        virtual String * hostname();
        
        virtual void setPort(unsigned int port);
        virtual unsigned int port();
        
        virtual void setUsername(String * username);
        virtual String * username();
        
        virtual void setPassword(String * password);
        virtual String * password();
        
        // To authenticate using OAuth2, username and oauth2token should be set.
        // auth type to use is AuthTypeOAuth2.
        virtual void setOAuth2Token(String * token);
        virtual String * OAuth2Token();
        
        virtual void setAuthType(AuthType authType);
        virtual AuthType authType();
        
        virtual void setConnectionType(ConnectionType connectionType);
        virtual ConnectionType connectionType();
        
        virtual void setTimeout(time_t timeout);
        virtual time_t timeout();
        
        virtual void setCheckCertificateEnabled(bool enabled);
        virtual bool isCheckCertificateEnabled();

        // When enabled, a TLS handshake that fails at every safer setting is
        // retried with OpenSSL's security level 0, which permits SHA-1 signed
        // certificates and undersized RSA/DH parameters. Off by default.
        virtual void setObsoleteTLSAllowed(bool allowed);
        virtual bool isObsoleteTLSAllowed();

        // Reason reported by the TLS backend for the last failed handshake,
        // e.g. "dh key too small". NULL when the last connect did not fail
        // during the handshake.
        virtual String * lastTLSErrorDescription();

        // Compatibility level the last connect() settled on. See
        // MAILSTREAM_SSL_COMPAT_* in libetpan's mailstream_ssl.h.
        virtual int tlsCompatibilityLevel();

        virtual String * lastSMTPResponse();

        virtual int lastSMTPResponseCode();
        
        virtual void connect(ErrorCode * pError);
        virtual void disconnect();
        
        virtual void login(ErrorCode * pError);
        
        virtual void checkAccount(Address * from, ErrorCode * pError);
        
        virtual void sendMessage(Data * messageData, SMTPProgressCallback * callback, ErrorCode * pError);
        virtual void sendMessage(Address * from, Array * /* Address */ recipients, Data * messageData,
                                 SMTPProgressCallback * callback, ErrorCode * pError);
        virtual void sendMessage(Address * from, Array * /* Address */ recipients, String * messagePath,
                                 SMTPProgressCallback * callback, ErrorCode * pError);

        virtual void cancelMessageSending();

        virtual void setConnectionLogger(ConnectionLogger * logger);
        virtual ConnectionLogger * connectionLogger();
        
        virtual void noop(ErrorCode * pError);
        
    public: // private
        virtual void lockConnectionLogger();
        virtual void unlockConnectionLogger();
        virtual ConnectionLogger * connectionLoggerNoLock();

        // Read by the TLS handshake callback, which is a plain C callback.
        int mTLSCompatibilityLevel;

    private:
        bool mObsoleteTLSAllowed;
        String * mLastTLSErrorDescription;
        String * mHostname;
        unsigned int mPort;
        String * mUsername;
        String * mPassword;
        String * mOAuth2Token;
        AuthType mAuthType;
        ConnectionType mConnectionType;
        time_t mTimeout;
        bool mCheckCertificateEnabled;
        bool mUseHeloIPEnabled;
        bool mShouldDisconnect;
        bool mSendingCancelled;
        bool mCanCancel;
        
        mailsmtp * mSmtp;
        SMTPProgressCallback * mProgressCallback;
        int mState;
        String * mLastSMTPResponse;
        int mLastLibetpanError;
        int mLastErrorLocation;
        int mLastSMTPResponseCode;
        pthread_mutex_t mCancelLock;
        pthread_mutex_t mCanCancelLock;
        
        ConnectionLogger * mConnectionLogger;
        pthread_mutex_t mConnectionLoggerLock;

        bool mOutlookServer;

        void init();
        Data * dataWithFilteredBcc(Data * data);
        static void body_progress(size_t current, size_t maximum, void * context);
        void bodyProgress(unsigned int current, unsigned int maximum);
        void setup();
        void unsetup();
        void connectWithCurrentCompatibilityLevel(ErrorCode * pError);
        void connectIfNeeded(ErrorCode * pError);
        bool checkCertificate();
        void setSendingCancelled(bool isCancelled);
        
        void sendMessage(MessageBuilder * msg, SMTPProgressCallback * callback, ErrorCode * pError);
        void internalSendMessage(Address * from, Array * /* Address */ recipients, Data * messageData,
                                 SMTPProgressCallback * callback, ErrorCode * pError);

    public: // private
        virtual bool isDisconnected();
        virtual void loginIfNeeded(ErrorCode * pError);
        virtual void saveLastResponse();
        
        int lastLibetpanError();
        int lastLibetpanErrorLocation();
    };
    
}

#endif

#endif
