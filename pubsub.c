#include "server.h"

/* Publish a message */
int pubsubPublishMessage(robj *channel, robj *message) {
    int receivers = 0;
    // dictEntry *de;
    // listNode *ln;
    // listIter li;

    // /* Send to clients listening for that channel */
    // de = dictFind(server.pubsub_channels,channel);
    // if (de) {
    //     list *list = dictGetVal(de);
    //     listNode *ln;
    //     listIter li;

    //     listRewind(list,&li);
    //     while ((ln = listNext(&li)) != NULL) {
    //         client *c = ln->value;

    //         addReply(c,shared.mbulkhdr[3]);
    //         addReply(c,shared.messagebulk);
    //         addReplyBulk(c,channel);
    //         addReplyBulk(c,message);
    //         receivers++;
    //     }
    // }
    // /* Send to clients listening to matching channels */
    // if (listLength(server.pubsub_patterns)) {
    //     listRewind(server.pubsub_patterns,&li);
    //     channel = getDecodedObject(channel);
    //     while ((ln = listNext(&li)) != NULL) {
    //         pubsubPattern *pat = ln->value;

    //         if (stringmatchlen((char*)pat->pattern->ptr,
    //                             sdslen(pat->pattern->ptr),
    //                             (char*)channel->ptr,
    //                             sdslen(channel->ptr),0)) {
    //             addReply(pat->client,shared.mbulkhdr[4]);
    //             addReply(pat->client,shared.pmessagebulk);
    //             addReplyBulk(pat->client,pat->pattern);
    //             addReplyBulk(pat->client,channel);
    //             addReplyBulk(pat->client,message);
    //             receivers++;
    //         }
    //     }
    //     decrRefCount(channel);
    // }
    return receivers;
}