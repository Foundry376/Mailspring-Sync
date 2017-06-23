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

void CommStream::readXBytes(unsigned int x, void* buffer)
{
    ssize_t bytesRead = 0;
    ssize_t result;
    while (bytesRead < x) {
        result = read(_socket, (char *)buffer + bytesRead, x - bytesRead);
        if (result < 1) {
            perror("read");
            throw "read interrupted";
        }
        
        bytesRead += result;
    }
}

json CommStream::waitForJSON() {
    try {
        unsigned int length = 0;
        char* buffer = 0;

        // we assume that sizeof(length) will return 4 here.
        readXBytes(sizeof(length), (void*)(&length));
        buffer = new char[length + 1];
        buffer[length] = '\0';
        readXBytes(length, (void*)buffer);
        
        // Then process the data as needed.
        json j = json::parse(buffer);
        delete [] buffer;
        return j;
    } catch (char const * e) {
        return {};
    }
}

void CommStream::sendJSON(json & msgJSON) {
    std::string str = msgJSON.dump() + "\n";
    const char * chars = str.c_str();

    size_t total = 0;
    size_t length = strlen(chars);
    while (total < strlen(chars)) {
        ssize_t n = send(_socket, chars + total, length, 0);
        if (n < 0) {
            perror("send");
            exit(1);
        }
        total += n;
        length -= n;
    }
}

void CommStream::didPersistModel(MailModel * model) {
    json msg = {
        {"type", "persist"},
        {"objectClass", model->tableName()},
        {"object", model->toJSON()},
    };
    sendJSON(msg);
}

void CommStream::didUnpersistModel(MailModel * model) {
    json msg = {
        {"type", "unpersist"},
        {"objectClass", model->tableName()},
        {"object", model->toJSON()},
    };
    sendJSON(msg);
}

