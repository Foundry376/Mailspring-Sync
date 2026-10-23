//
//  MCCertificateUtils.h
//  mailcore2
//
//  Created by DINH Viêt Hoà on 7/25/13.
//  Copyright (c) 2013 MailCore. All rights reserved.
//

#ifndef MAILCORE_MCCERTIFICATEUTILS_H

#define MAILCORE_MCCERTIFICATEUTILS_H

#include <libetpan/libetpan.h>
#include <MailCore/MCString.h>
#include <MailCore/MCArray.h>
#include <MailCore/MCData.h>

namespace mailcore {

    bool checkCertificate(mailstream * stream, String * hostname);

    // The certificates the server presented on the stream, leaf first, as DER Data.
    Array * certificateChain(mailstream * stream);

    // Lowercase hex SHA-256 of a DER certificate, the form in which a user accepts one.
    String * certificateSHA256(Data * der);

    // Whether the chain's leaf is the certificate the user accepted. Lets a server whose
    // certificate fails validation be used after the user has reviewed that exact certificate.
    bool isAcceptedCertificate(Array * chain, String * acceptedSHA256);

}

#endif
