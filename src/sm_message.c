#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "sm_message.h"
#include "config.h"

/*
 * Return 0 if all bytes are successfully sent, otherwise returns 1
*/
int sm_send(int socket, msg_t *message) {
    int sent = 0, bytes = 0;
    while (sent < message->len) {
        bytes = send(socket, message->buffer + sent, message->len - sent, 0);
        sent += bytes;
    }

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