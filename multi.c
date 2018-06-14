#include "server.h"

typedef struct watchedKey {
    robj *key;
    redisDb *db;
} watchedKey;

/* Unwatch all the keys watched by this client. To clean the EXEC dirty
 * flag is up to the caller. */
void unwatchAllKeys(client *c) {
    listIter li;
    listNode *ln;

    if (listLength(c->watched_keys) == 0) return;
    listRewind(c->watched_keys,&li);
    while((ln = listNext(&li))) {
        list *clients;
        watchedKey *wk;

        /* Lookup the watched key -> clients list and remove the client
         * from the list */
        wk = listNodeValue(ln);
        clients = dictFetchValue(wk->db->watched_keys, wk->key);
        serverAssertWithInfo(c,NULL,clients != NULL);
        listDelNode(clients,listSearchKey(clients,c));
        /* Kill the entry at all if this was the only client */
        if (listLength(clients) == 0)
            dictDelete(wk->db->watched_keys, wk->key);
        /* Remove this watched key from the client->watched list */
        listDelNode(c->watched_keys,ln);
        decrRefCount(wk->key);
        zfree(wk);
    }
}


void discardCommand(client *c) {
    if (!(c->flags & CLIENT_MULTI)) {
        addReplyError(c,"DISCARD without MULTI");
        return;
    }
    // discardTransaction(c);
    // addReply(c,shared.ok);
}

void watchCommand(client *c) {
    int j;

    if (c->flags & CLIENT_MULTI) {
        addReplyError(c,"WATCH inside MULTI is not allowed");
        return;
    }
    // for (j = 1; j < c->argc; j++)
    //     watchForKey(c,c->argv[j]);
    // addReply(c,shared.ok);
}

/* Flag the transacation as DIRTY_EXEC so that EXEC will fail.
 * Should be called every time there is an error while queueing a command. */
void flagTransaction(client *c) {
    if (c->flags & CLIENT_MULTI)
        c->flags |= CLIENT_DIRTY_EXEC;
}

/* Add a new command into the MULTI commands queue */
void queueMultiCommand(client *c) {
    multiCmd *mc;
    int j;

    c->mstate.commands = zrealloc(c->mstate.commands,
            sizeof(multiCmd)*(c->mstate.count+1));
    mc = c->mstate.commands+c->mstate.count;
    mc->cmd = c->cmd;
    mc->argc = c->argc;
    mc->argv = zmalloc(sizeof(robj*)*c->argc);
    memcpy(mc->argv,c->argv,sizeof(robj*)*c->argc);
    for (j = 0; j < c->argc; j++)
        incrRefCount(mc->argv[j]);
    c->mstate.count++;
}

void multiCommand(client *c) {
    if (c->flags & CLIENT_MULTI) {
        addReplyError(c,"MULTI calls can not be nested");
        return;
    }
    c->flags |= CLIENT_MULTI;
    addReply(c,shared.ok);
}

void execCommand(client *c) {
    int j;
    robj **orig_argv;
    int orig_argc;
    struct redisCommand *orig_cmd;
    int must_propagate = 0; /* Need to propagate MULTI/EXEC to AOF / slaves? */

    if (!(c->flags & CLIENT_MULTI)) {
        addReplyError(c,"EXEC without MULTI");
        return;
    }

    /* Check if we need to abort the EXEC because:
     * 1) Some WATCHed key was touched.
     * 2) There was a previous error while queueing commands.
     * A failed EXEC in the first case returns a multi bulk nil object
     * (technically it is not an error but a special behavior), while
     * in the second an EXECABORT error is returned. */
//     if (c->flags & (CLIENT_DIRTY_CAS|CLIENT_DIRTY_EXEC)) {
//         addReply(c, c->flags & CLIENT_DIRTY_EXEC ? shared.execaborterr :
//                                                   shared.nullmultibulk);
//         discardTransaction(c);
//         goto handle_monitor;
//     }

//     /* Exec all the queued commands */
//     unwatchAllKeys(c); /* Unwatch ASAP otherwise we'll waste CPU cycles */
//     orig_argv = c->argv;
//     orig_argc = c->argc;
//     orig_cmd = c->cmd;
//     addReplyMultiBulkLen(c,c->mstate.count);
//     for (j = 0; j < c->mstate.count; j++) {
//         c->argc = c->mstate.commands[j].argc;
//         c->argv = c->mstate.commands[j].argv;
//         c->cmd = c->mstate.commands[j].cmd;

//         /* Propagate a MULTI request once we encounter the first write op.
//          * This way we'll deliver the MULTI/..../EXEC block as a whole and
//          * both the AOF and the replication link will have the same consistency
//          * and atomicity guarantees. */
//         if (!must_propagate && !(c->cmd->flags & CMD_READONLY)) {
//             execCommandPropagateMulti(c);
//             must_propagate = 1;
//         }

//         call(c,CMD_CALL_FULL);

//         /* Commands may alter argc/argv, restore mstate. */
//         c->mstate.commands[j].argc = c->argc;
//         c->mstate.commands[j].argv = c->argv;
//         c->mstate.commands[j].cmd = c->cmd;
//     }
//     c->argv = orig_argv;
//     c->argc = orig_argc;
//     c->cmd = orig_cmd;
//     discardTransaction(c);
//     /* Make sure the EXEC command will be propagated as well if MULTI
//      * was already propagated. */
//     if (must_propagate) server.dirty++;

// handle_monitor:
//     /* Send EXEC to clients waiting data from MONITOR. We do it here
//      * since the natural order of commands execution is actually:
//      * MUTLI, EXEC, ... commands inside transaction ...
//      * Instead EXEC is flagged as CMD_SKIP_MONITOR in the command
//      * table, and we do it here with correct ordering. */
//     if (listLength(server.monitors) && !server.loading)
//         replicationFeedMonitors(c,server.monitors,c->db->id,c->argv,c->argc);
}