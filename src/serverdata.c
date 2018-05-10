#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "headers/serverdata.h"
#include "headers/serverlog.h"

/*******************************************************************************
 *                    Communication Structures and Helpers                     *
 ******************************************************************************/

/*
 * Add a new file descriptor to the servers current fd_set. Once a fd is added,
 * the server will begin reading and writing to it. The maxfd value is updated 
 * if applicable.
 *
 * @param fd
 *        the file descriptor to add to the fd_set
 * @param connections
 *        the connections struct holding all currently active connections on the
 *        server
 */
void add_fd(int fd, connections_t *connections)
{
    FD_SET(fd, connections->all_fds);
    fcntl(fd, F_SETFL, O_NONBLOCK);

    if (fd > connections->maxfd)
    {
        connections->maxfd = fd;
    }
}

/*
 * Remove a file descriptor from the servers current fd_set. Once a fd is removed,
 * the server will not be able to read and write to it. This closes the clients
 * or jobs pipes once they have exited.
 *
 * @param fd
 *        the file descriptor to remove from the fd_set
 * @param connections
 *         the connections struct holding all currently active connections on the
 *        server
 */
void close_fd(int fd, connections_t *connections)
{
    FD_CLR(fd, connections->all_fds);
}

/*******************************************************************************
 *                       Client Structures and Helpers                         *
 ******************************************************************************/

/*
 * Add a client that has connected to the server to the curent clientlist.
 * Update the clientlist pointers and size count. On error, the appropiate 
 * message is written to stderr.
 *
 * @param clientfd
 *        the clients file descriptor to communicate through
 * @param clientlist
 *        the list of clients currently active on the server
 *
 * @return
 *        -1:        an error occured and the client could not be added to the
 *                   client list
 *        0:         client was successfully added to the clientlist
 */
int add_client(int clientfd, clientlist_t *clientlist)
{
    client_t *new_client = malloc(sizeof(struct client));
    
    if (new_client == NULL)
    {
        return -1;
    }

    /* Fill clients information */
    new_client->clientfd = clientfd;
    new_client->next = NULL;
    new_client->prev = clientlist->end;
    
    /* Append client to the clientlist */
    if (clientlist->size == 0)
    {
        clientlist->head = clientlist->end = new_client;
    }
    else
    {
        clientlist->end->next = new_client;
        clientlist->end = new_client;
    }

    add_fd(clientfd, clientlist->fdset); /* Allow read/write from server */
    clientlist->size++;
    return 0;
}

/*
 * Remove a client that has connected to the server to the curent clientlist.
 * Update the clientlist pointers and size count. All mallocs will be freed.
 *
 * @param client
 *        the client to remove from the clientlist
 * @param clientlist
 *        the list of clients currently active on the server
 */
void close_client(client_t *client, clientlist_t *clientlist)
{
    if (client->next == NULL)
    {
        if (client->prev == NULL) /* Only client */
        {
            clientlist->head = clientlist->end = NULL;
        }
        else /* Last client */
        {
            clientlist->end = client->prev;
            clientlist->end->next = NULL;
        }
    }
    else 
    {
        if (client->prev == NULL) /* First client */
        {
            clientlist->head = clientlist->head->next;
            clientlist->head->prev = NULL;
        }
        else /* Middle client */
        {
            client->next->prev = client->prev;
            client->prev->next = client->next;
        }
    }
    
    /* TODO Log this */
    printf("[CLIENT %d] Connection closed\n", client->clientfd);

    close_fd(client->clientfd, clientlist->fdset);
    free_client(client);
    clientlist->size--;
}

/*
 * Clean up the client by closing its file descriptor, removing its pointers
 * and freeing any mallocs. This should only be called by close_client().
 *
 * @param client
 *        the client to close and clean
 */
void free_client(client_t *client)
{
    close(client->clientfd);
    client->clientfd = -1;
    client->next = NULL;
    client->prev = NULL;
    free(client);
}

/*
 * Clear all the clients in the clientlist given. Each client will be cleaned up 
 * (see free_client()) and the clientlist will be destroyed. This should only be
 * called when the server is shutting down. clientlist size_t is not updated.
 *
 * @param clientlist
 *        the clientlist to clear and destory
 */
void clear_clients(clientlist_t *clientlist)
{
    notify_clients_shutdown(clientlist);
    client_t *temp = clientlist->head;

    if (temp == NULL)
    {
        free(clientlist);
        return;
    }
    while (temp) /* Clean up each client */
    {
        client_t *next = temp->next;
        free(temp);
        temp = next;
    }
    clientlist->head = NULL;
    clientlist->end = NULL;
    free(clientlist);
}

/*******************************************************************************
 *                        Job Structures and Helpers                           *
 ******************************************************************************/

/*
 * Add the job to the list of currently running jobs. Assign the client that 
 * invoked the job as the first watcher to be notified of the jobs output. 
 * Set-up a job_t struct for the new job, fill it, then append it to the end of
 * the joblist given. On error, the appropiate message is written to stderr.
 *
 * @param pid
 *        the pid of the newly created job
 * @param mpid
 *        the pid of the newly created job manager
 * @param jobpipe
 *        the read end FD of the newly created job
 * @param client
 *         the client who invoked the call (i.e. the first watcher)
 * @param joblist
 *         the list of currently running jobs to append to the new job too
 *
 * @return
 *         -1:      an error occured and the job could not be created or 
 *                  appended, or no watchers could be assigned to the job
 *         0:       the job was successfully created and appended
 */
int add_job(pid_t pid, pid_t mpid, int jobpipe, client_t *client, 
            joblist_t *joblist)
{
    if (joblist->size == MAX_JOBS)
    {
        return -1;
    }

    job_t *job;
    job = malloc(sizeof(struct job));
    if (job == NULL)
    {
        return -1;
    }

    /* Fill the job struct of the jobs information */
    job->pid = pid;
    job->mpid = mpid;
    job->jobpipe = jobpipe;
    job->next = NULL;
    job->prev = NULL;

    /* Prepare to assign watchers to the job */
    job->watchlist = malloc(sizeof(struct watchlist));
    if (job->watchlist == NULL)
    {
        return -1;
    }

    job->watchlist->head = job->watchlist->end = NULL;
    job->watchlist->size = 0;

    /* Append the job to the joblist */
    if (joblist->head == NULL)
    {
        joblist->head = job;
        joblist->end = job;
    }
    else
    {
        joblist->end->next = job;
        job->prev = joblist->end;
        joblist->end = job;
    }

    joblist->size++;
    
    add_fd(jobpipe, joblist->fdset);

    /* Assign client as the first watcher of the job */
    if (add_watcher(pid, client, joblist) < 0)
    {
        remove_job(pid, joblist); /* Avoid running unwatchable jobs */
    }
    return 0;    
}

/*
 * Remove a currently running job from the joblist and disconnect all
 * communication between the server and the job. This should only be called
 * after the job's process has exited, as once the job is removed their will
 * be access to its process anymore.
 *
 * @param pid
 *        the pid of the job's process that will be removed from the joblist
 * @param joblist
 *        the list of jobs to remove the job from
 *
 * @return
 *        -1:           the job pid specified does not refer to a currently running
 *                      job within the joblist given
 *        0:            the job was successfully removed from the joblist
 */
int remove_job(pid_t pid, joblist_t *joblist)
{
    job_t *job = find_job(pid, joblist);
    int stat;
    waitpid(job->mpid, &stat, 0);
    if (job == NULL)
    {
        return -1;
    }

    if (joblist->size == 1)
    {
        joblist->head = NULL;
        joblist->end = NULL;
    }
    else if (jobcmp(job, joblist->head)) /* First job */
    {
        joblist->head = joblist->head->next;
        joblist->head->prev = NULL;
    }
    
    else if (jobcmp(job, joblist->end)) /* Last Job */
    {
        joblist->end = joblist->end->prev;
        joblist->end->next = NULL;
    }
    else
    {
        job_t *prev_job = job->prev;
        job_t *next_job = job->next;
        
        next_job->prev = prev_job;
        prev_job->next = next_job;        
    }

    close_fd(job->jobpipe, joblist->fdset);
    free_job(job);
    joblist->size--;
    return 0;
}

/*
 * Find and return the job in the given joblist that is running with the pid
 * exactly that of the specified pid. 
 *
 * @param pid
 *        the pid of the job process desired
 * @param joblist
 *        the list of jobs to find the job within
 *
 * @return
 *        NULL:        if the job does not exist
 *        job:         pointer to the desired job with the specified pid
 */
job_t *find_job(pid_t pid, joblist_t *joblist)
{
    /* No jobs running */
    if (joblist == NULL || joblist->size == 0 || joblist->head == NULL) 
    {
        return NULL;
    }
    if (joblist->size == 1) /* Only one job */
    {
        return joblist->head;
    }

    job_t *temp = joblist->head;

    while (temp) /* Scan the list for the pid */
    {
        if (temp->pid == pid)
        {
            return temp;
        }
        temp = temp->next; 
    }
    return NULL;
}

/*
 * Determine if the two jobs refer to the same job or if they are unique.
 *
 * @param job1 job2
 *         the two jobs to comapre
 *
 *    @return
 *        0:            the two jobs are unique
 *        1:            the two jobs are the same
 */
int jobcmp(job_t *job1, job_t *job2)
{
    if (job1 == NULL || job2 == NULL)
    {
        return 0;
    }    
    return job1 == job2;
}

/*
 * Clean up and close the given job, free any mallocs, clear all watchers,
 * close the jobpipe and dereference all pointers.
 *
 * @param job
 *        the job to clean up and close
 */
void free_job(job_t *job)
{
    job->next = NULL;
    job->prev = NULL;

    watcher_t *watcher = job->watchlist->head;

    while (watcher) /* Remove watchers */
    {
        watcher_t *temp = watcher;
        watcher = watcher->next;
        free(temp);
    }

    close(job->jobpipe);
    free(job->watchlist);
    free(job);
}

/*
 * Clear all the jobs in the joblist given. Each job will be cleaned up (see
 * free_job()) and the joblist will be destroyed. This should only be called
 * when the server is shutting down. joblist size_t is not updated.
 *
 * @param joblist
 *        the joblist to clear and destory
 */
void clear_jobs(joblist_t *joblist)
{
    job_t *temp = joblist->head;

    if (temp == NULL)
    {
        free(joblist);
        return;
    }
    while (temp) /* Clean up each job */
    {
        job_t *next = temp->next;
        free_job(temp);
        temp = next;
    }
    joblist->head = NULL;
    joblist->end = NULL;
    free(joblist);
}

/*******************************************************************************
 *                    Job Watcher Structures and Helpers                       *
 ******************************************************************************/

/*
 * Add a client to the watchlist of the job specified by pid. Clients watching
 * a job will be sent all its output as well as the jobs exit status. This is no
 * bound on the size of the watchlsit for a given job. On error, the appropiate 
 * message is written to stderr.
 *
 * @param pid
 *        the pid of the job to assign the watcher too
 * @param client
 *        the client to add to the watchlist of the specified job
 * @param joblist
 *        the joblist that contains the job specified by pid
 *
 * @return
 *        -1:           an error occured and the watcher was not added to the 
 *                      jobs watchlist
 *        0:            the client was successfully added to the jobs watchlist
 *                      or was successfully removed if the client was already 
 *                      watching the job
 *        1:            the job specified by pid is not a currently running job
 */
int add_watcher(pid_t pid, client_t *client, joblist_t *joblist)
{
    job_t *job = find_job(pid, joblist);
    
    if (job == NULL)
    {
        return 1;
    }
    
    watcher_t *watcher = find_watcher(client, job->watchlist);
    if (watcher != NULL) /* Client was already watching */
    {
        remove_watcher(watcher, job->watchlist);
        return 0;
    }
    
    watcher = malloc(sizeof(struct watcher));
    if (watcher == NULL)
    {
        perror("malloc");
        return -1;
    }

    watcher->client = client;
    watcher->prev = NULL;
    watcher->next = NULL;

    if (job->watchlist->size == 0) /* Job was jsut created or was solo */
    {
        job->watchlist->head = job->watchlist->end = watcher;
    }
    else /* Multiple watchers so watcher is appended */
    {
        job->watchlist->end->next = watcher;
        watcher->prev = job->watchlist->end;
        job->watchlist->end = watcher;
    }
    job->watchlist->size++;
    return 0;
}

/*
 * Remove a client to the watchlist of the job specified by pid. The client that
 * was previously watching the job will no longer be sent any of its output as 
 * nor the jobs exit status. On error, the appropiate message is written to 
 * stderr.
 *
 * @param wacther
 *        the watcher to remove from the watchlist of the specified job
 * @param watchlist
 *        the watchlist that contains the watcher to be removed for a given job
 */
void remove_watcher(watcher_t *watcher, watchlist_t *watchlist)
{
    if (watchlist->size == 1) /* Only watcher */
    {
        watchlist->head = watchlist->end = NULL;
    }
    else if (watchercmp(watcher, watchlist->head)) /* Watcher was at the head */
    {
        watchlist->head = watchlist->head->next;
        watchlist->head->prev = NULL;
    }
    else if (watchercmp(watcher, watchlist->end)) /* Watcher was at the end */
    {
        watchlist->end = watchlist->end->prev;
        watchlist->end->next = NULL;
    }
    else /* Watcher was somehwere inbetween */
    {
        watcher_t *pwatcher = watcher->prev;
        watcher_t *nwatcher = watcher->next;
        nwatcher->prev = pwatcher;
        pwatcher->next = nwatcher;
    }
    
    watchlist->size--;
    free(watcher);
}

/*
 * Locate the client (watcher) in a job's watchlist and return the watcher_t
 * that contains the client. If the client is not found in the watchlist, then
 * the client was not watching the given job.
 *
 * @param client
 *        the client to locate in the specified job's watchlist
 * @param watchlist
 *        the watchlist that contains all the watchers of the specified job
 *
 * @return
 *        NULL:         the client was not watching the given job, and thus was 
 *                      not in the jobs watchlist
 *        watcher_t:    the watcher struct containing the client specified
 */
watcher_t *find_watcher(client_t *client, watchlist_t *watchlist)
{
    /* No clients currently watch the job */
    if (watchlist == NULL || watchlist->size == 0 || watchlist->head == NULL)
    {
        return NULL;
    }
    if (watchlist->size == 1) /* One client watching */
    {
        return watchlist->head;
    }
    watcher_t *watcher = watchlist->head;
    while (watcher) /* Locate client */
    {
        if (watcher->client == client) /* TODO CHANGE BACK TO CLIENTFD IF FAILS*/
        {
            return watcher;
        }
        watcher = watcher->next;
    }
    return NULL;
}

/*
 * Determine if the two watchers refer to the same client or if they are unique.
 *
 * @param watcher1 watcher2
 *        the two watchers to compare equality
 *
 * @return
 *        0:            the two watchers are not equivalent and thus point to 
 *                      unique clients
 *        1:            the watchers refer to the same client
 */
int watchercmp(watcher_t *watcher1, watcher_t *watcher2)
{
    if (watcher1 == NULL || watcher2 == NULL)
    {
        return 0;
    }
    return watcher1 == watcher2;
}
