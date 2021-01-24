#include <stdlib.h>

#define HEADER_LEN 3 /* message_header = {message.type, message.len, message.nid} */
#define SM_MSG_MAX 128

/*  */
typedef struct sm_message {
    char type; /* The type of message */
    char len;  /* The length of the message */
    char nid;  /* The node id of sender (allocator == -1) */
    char buffer[SM_MSG_MAX]; /* the message (includes header and body) */
} msg_t;

/* 
 * The identifiers for messages (the type in the above struct)
 *
 * Replies are sent back generally as an acknowledgement, but for the case of read faults (as an 
 * example) may also be used to send back non-resident memory from the allocator -> node
 *
 * The comments after the message identifiers indicate the expected message body format
*/
#define SM_INIT       0 // {}
#define SM_INIT_REPLY 1 // {nid}
#define SM_EXIT       2 // {}
#define SM_EXIT_REPLY 3 // {}
#define SM_BARR       4 // {}
#define SM_BARR_REPLY 5 // {}
#define SM_ALOC       6 // {size}
#define SM_ALOC_REPLY 7 // {offset}
#define SM_CAST       8 // {root_nid, value}
#define SM_CAST_REPLY 9 // {value}
#define SM_READ       10 // {milk}
#define SM_READ_REPLY 11 // {milk}
#define SM_WRIT       12 // {milk}
#define SM_WRIT_REPLY 13 // {milk}


msg_t *sm_msg_create(char nid, char type, char buffer[]);
int    sm_msg_free  (msg_t *message);
int    sm_send      (char nid, char type, char buffer[]);
int    sm_recv      (int socket, msg_t **message);
int    sm_recv_type (int socket, msg_t **messsage, int type);