#include <stdlib.h>
 
/*  */
typedef struct sm_message {
    char type; /* The type of message */
    char len;  /* The length of the message */
    char nid;  /* The node id of sender (allocator == -1) */
    char *buffer; /* the message (includes a header and body) */
} msg_t;

#define HEADER_LEN 3 /* message_header = {message.type, message.len, message.nid} */

/* 
 * The identifiers for messages (the type in the above struct)
 *
 * Replies are sent back generally as an acknowledgement, but for the case of read faults (as an 
 * example) may also be used to send back non-resident memory from the allocator -> node
 *
 * The comments after the message identifiers indicate the expected message body format
*/
#define SM_INIT        0 // {}
#define SM_INIT_REPLY  1 // {}
#define SM_EXIT        2 // {}
#define SM_EXIT_REPLY  3 // {}
#define SM_BARR        4 // {}
#define SM_BARR_REPLY  5 // {}
#define SM_ALLOC       6 // {size}
#define SM_ALLOC_REPLY 7 // {offset}
#define SM_BCAST       8 // {root_nid, value}
#define SM_BCAST_REPLY 9 // {value}
