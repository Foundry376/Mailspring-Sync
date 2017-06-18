//
//  CommStream.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "CommStream.hpp"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>

using json = nlohmann::json;

////char *socket_path = "./socket";
//char *socket_path = "\0hidden";

CommStream::CommStream(char * socket_path) {
    int s;
    
    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    
    printf("Trying to connect...\n");
    
    struct sockaddr_un remote;
    memset(&remote, 0, sizeof(struct sockaddr_un));

    strcpy(remote.sun_path, "/tmp/cmail.sock");
    remote.sun_family = AF_UNIX;
    remote.sun_len = SUN_LEN(&remote);
    
    if (connect(s, (struct sockaddr *)&remote, remote.sun_len) == -1) {
        perror("connect");
        exit(1);
    }
    
    printf("Connected.\n");
    
    _socket = s;
}


CommStream::~CommStream() {
    close(_socket);
}

void CommStream::sendJSON(json msgJSON) {
    const char * chars = msgJSON.dump().c_str();

    if (send(_socket, chars, strlen(chars), 0) == -1) {
        perror("send");
        exit(1);
    }
}

void CommStream::didPersistModels(std::string modelName, std::vector<std::string> * ids) {
    json msg = {
        {"pi", 3.141},
        {"model", modelName},
    };
    sendJSON(msg);
}
