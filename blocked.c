#include "server.h"
/* Unblock a client calling the right function depending on the kind
 * of operation the client is blocking for. */
void unblockClient(client *c) {
    // if (c->btype == BLOCKED_LIST) {
    //     unblockClientWaitingData(c);
    // } else if (c->btype == BLOCKED_WAIT) {
    //     unblockClientWaitingReplicas(c);
    // } else {
    //     serverPanic("Unknown btype in unblockClient().");
    // }
    // /* Clear the flags, and put the client in the unblocked list so that
    //  * we'll process new commands in its query buffer ASAP. */
    // c->flags &= ~CLIENT_BLOCKED;
    // c->btype = BLOCKED_NONE;
    // server.bpop_blocked_clients--;
    // /* The client may already be into the unblocked list because of a previous
    //  * blocking operation, don't add back it into the list multiple times. */
    // if (!(c->flags & CLIENT_UNBLOCKED)) {
    //     c->flags |= CLIENT_UNBLOCKED;
    //     listAddNodeTail(server.unblocked_clients,c);
    // }
}