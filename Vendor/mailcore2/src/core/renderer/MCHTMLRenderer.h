//
//  MCHTMLRenderer.h
//  testUI
//
//  Created by DINH Viêt Hoà on 1/23/13.
//  Copyright (c) 2013 MailCore. All rights reserved.
//

#ifndef MAILCORE_MCHTMLRENDERER_H

#define MAILCORE_MCHTMLRENDERER_H

#include <MailCore/MCAbstract.h>
#include <MailCore/MCIMAP.h>
#include <MailCore/MCRFC822.h>

#ifdef __cplusplus

namespace mailcore {
    
    class MessageParser;
    class HTMLRendererTemplateCallback;
    class HTMLRendererIMAPCallback;
    class HTMLRendererRFC822Callback;
    
    class MAILCORE_EXPORT HTMLRenderer {
    public:
        static String * htmlForRFC822Message(MessageParser * message,
                                             HTMLRendererRFC822Callback * dataCallback,
                                             HTMLRendererTemplateCallback * htmlCallback);
        
        static String * htmlForIMAPMessage(String * folder,
                                           IMAPMessage * message,
                                           HTMLRendererIMAPCallback * dataCallback,
                                           HTMLRendererTemplateCallback * htmlCallback);
        
        // BG: Expose this so that we can avoid running this renderer three times to retrieve
        // attachments, relatedAttachments, message HTML separately.
        static String * htmlForRFC822MessageAndAttachments(MessageParser * message,
                                               HTMLRendererIMAPCallback * imapDataCallback,
                                               HTMLRendererRFC822Callback * rfc822DataCallback,
                                               HTMLRendererTemplateCallback * htmlCallback,
                                               Array * attachments,
                                               Array * relatedAttachments);

        static Array * /* AbstractPart */ attachmentsForMessage(AbstractMessage * message);
        static Array * /* AbstractPart */ htmlInlineAttachmentsForMessage(AbstractMessage * message);
        static Array * /* AbstractPart */ requiredPartsForRendering(AbstractMessage * message);
    };
    
};

#endif

#endif
