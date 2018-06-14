#include "cluster.h"
/* -----------------------------------------------------------------------------
 * Cluster functions related to serving / redirecting clients
 * -------------------------------------------------------------------------- */

/* The ASKING command is required after a -ASK redirection.
 * The client should issue ASKING before to actually send the command to
 * the target instance. See the Redis Cluster specification for more
 * information. */
void askingCommand(client *c) {
    if (server.cluster_enabled == 0) {
        addReplyError(c,"This instance has cluster support disabled");
        return;
    }
    c->flags |= CLIENT_ASKING;
    addReply(c,shared.ok);
}

/* This function is called before the event handler returns to sleep for
 * events. It is useful to perform operations that must be done ASAP in
 * reaction to events fired but that are not safe to perform inside event
 * handlers, or to perform potentially expansive tasks that we need to do
 * a single time before replying to clients. */
void clusterBeforeSleep(void) {
    /* Handle failover, this is needed when it is likely that there is already
     * the quorum from masters in order to react fast. */
    // if (server.cluster->todo_before_sleep & CLUSTER_TODO_HANDLE_FAILOVER)
    //     clusterHandleSlaveFailover();

    // /* Update the cluster state. */
    // if (server.cluster->todo_before_sleep & CLUSTER_TODO_UPDATE_STATE)
    //     clusterUpdateState();

    // /* Save the config, possibly using fsync. */
    // if (server.cluster->todo_before_sleep & CLUSTER_TODO_SAVE_CONFIG) {
    //     int fsync = server.cluster->todo_before_sleep &
    //                 CLUSTER_TODO_FSYNC_CONFIG;
    //     clusterSaveConfigOrDie(fsync);
    // }

    // /* Reset our flags (not strictly needed since every single function
    //  * called for flags set should be able to clear its flag). */
    // server.cluster->todo_before_sleep = 0;
}