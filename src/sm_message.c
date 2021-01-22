#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "sm_message.h"
#include "config.h"

/* 
 * 
*/
msg_t *sm_msg_create(char nid, char type, char *buffer) {
    msg_t *message = malloc(sizeof(msg_t));

    message->type = type;
    message->len  = (char) (strlen(buffer) + 3);
    message->nid  = nid;

    /* Certain messages have empty bodies (e.g. initializatio) if this is the case the buffer will be empty */
    if (buffer == NULL) {
        message->buffer = NULL;
    } else {
        message->buffer = strndup(buffer, strlen(buffer));
    }

    return message;
}

/*
 * Return 0 if all bytes are successfully sent, otherwise returns 1
*/
int sm_send(char nid, char type, char *buffer) {
    msg_t *message = sm_msg_create(nid, type, buffer);
    int sent = 0, bytes = 0;

    while (sent < message->len) {
        bytes = send(socket, message->buffer + sent, message->len - sent, 0);
        sent += bytes;
    }

    if (message->buffer != NULL) free(message->buffer);
    if (message != NULL)         free(message);

    return (sent != message->len);
}

/*
 * 
*/
msg_t *sm_recv(int socket) {
    /* Allocate memory for the message */
    msg_t *message = malloc(1024 * sizeof(msg_t));

    /* Receive the message header */
    int status = recv(socket, message->buffer, HEADER_LEN, MSG_WAITALL);
    if (status == 0) {
        return NULL;
    }

    /* Extract the metadata from the received message */
    message->type = message->buffer[0];
    message->len  = message->buffer[1];
    message->nid  = message->buffer[2];

    /* Receive the message body */
    int recvd = 0, bytes = 0; // TODO: Allocate more memory when required
    while (recvd < message->len - HEADER_LEN) {
        bytes = recv(socket, &(message->buffer[HEADER_LEN + recvd]), 
                    message->len - HEADER_LEN - recvd, MSG_WAITALL);
        recvd += bytes;
    }

    return message;
}