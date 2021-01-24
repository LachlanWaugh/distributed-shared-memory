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
msg_t *sm_msg_create(char nid, char type, char buffer[]) {
    msg_t *message = malloc(sizeof(msg_t));
    memset(message, 0, sizeof(msg_t));

    message->buffer[0] = message->type = type;
    message->buffer[1] = message->len  = (char) (strlen(buffer) + HEADER_LEN);
    message->buffer[2] = message->nid  = nid;

    /* Fill the rest of the buffer with the message to be passed */
    if (buffer != NULL) {
        for (int i = 0; i < SM_MSG_MAX && i < strlen(buffer); i++) 
            message->buffer[i + 3] = buffer[i];
    }

    return message;
}

int sm_msg_free(msg_t *message) {
    free(message->buffer);
    free(message);

    return 0;
}

/*
 * Return 0 if all bytes are successfully sent, otherwise returns 1
*/
int sm_send(int socket, char nid, char type, char buffer[]) {
    msg_t *message = sm_msg_create(nid, type, buffer);
    int sent = 0, bytes = 0;

    while (sent < message->len) {
        bytes = send(socket, message->buffer + sent, message->len - sent, 0);
        sent += bytes;
    }

    if (message != NULL) free(message);

    return (sent != message->len);
}

/*
 * 
*/
int sm_recv(int socket, msg_t **buffer) {
    memset(*buffer, 0, sizeof(msg_t));
    /* Allocate memory for the message */
    msg_t *message = malloc(sizeof(msg_t));

    /* Receive the message header */
    int status = recv(socket, message->buffer, HEADER_LEN, MSG_WAITALL);
    if (status == 0) {
        return 1;
    }

    /* Extract the metadata from the received message */
    message->type   = message->buffer[0];
    message->len    = message->buffer[1];
    message->nid    = message->buffer[2];

    /* Receive the message body */
    int recvd = 0, bytes = 0; // TODO: Allocate more memory when required
    while (recvd < message->len - HEADER_LEN) {
        bytes = recv(socket, &(message->buffer[HEADER_LEN + recvd]), 
                    message->len - HEADER_LEN - recvd, MSG_WAITALL);
        recvd += bytes;
    }

    *buffer = message;
    return (recvd != message->len);
}

/* Receive a message of a specific type */
int sm_recv_type(int socket, msg_t **buffer, int type) {
    /* Allocate memory for the message */
    msg_t *message = malloc(sizeof(msg_t));

    int status = sm_recv(socket, &message);
    if (status) return status;

    *buffer = message;
    return (message->type == type);
}