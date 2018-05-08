#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#include "headers/socket.h"
#include "headers/jobprotocol.h"
#include "headers/jobcommands.h"
#include "headers/serverdata.h"
#include "headers/serverlog.h"

#define QUEUE_LENGTH 5

static int active = 1;

/*
 * When the server receives the kill signal, begin tear down phase
 * by breaking the 'server loop' in main().
 * 
 * @param sig
 *        Supported signals are only SIGINT (ctrl-c)
 */
void close_server_handler(int sig)
{
    active = 0;
}

/*
 * Accept a connection request from a potential client, adding them to the 
 * server's current clientlist. If successful, the client can issue any of the
 * supported commands.
 *
 * @param listenfd
 *        the servers listen to accept the client on
 * @param clientlist
 *        the clientlist that the new client will be appended too (if successful)
 */
void setup_client(int listenfd, clientlist_t *clientlist)
{
    int clientfd = accept_connection(listenfd);

    /* Connection failed or client could not be added to the clientlist */
    if (clientfd < 0 || add_client(clientfd, clientlist) < 0)
    {
        fprintf(stderr, CLIENT_ERROR);
        return; /* Server continues */
    }

    if (write_client(NULL, CLIENT_ACCPT, clientfd) < 0
        || write_client(NULL, CLIENT_WELCOME, clientfd) < 0)
    {
        return;
    }
}

/*
 * Read the output of the job forwarded by its manager and redirect it to all
 * of the jobs watchers. If the job exits, remove it from the joblist and notify
 * its watchers. If the client closes during the middle of watching a job,
 * remove it from the jobs watcherlist and notify the server to close its socket.
 *
 * @param job
 *        the job to read output from
 * @param joblist
 *        the list of active jobs on the server
 *
 * @return
 *        -1:               jobs pipe has closed, thus job is finished
 *        0:                job output was read and forwarded successfully
 */
int read_write_job(job_t* job, joblist_t *joblist)
{
    char buf[BUFSIZE+1];
    char *after = buf;
    int room = BUFSIZE;
    int inbuf = 0;
    int nbytes = 0;
    int stat;

    while (waitpid(job->mpid, &stat, WNOHANG) <= 0 &&
            (nbytes = read(job->jobpipe, after, room)) > 0)
    {
        inbuf += nbytes;
        int nwl;
    
        /* Only accept full commands */
        while ((nwl = find_network_newline(buf, inbuf)) > 0) 
        {
            buf[nwl-2] = '\0'; /* Remove \r\n */

            printf("%s", buf);
            inbuf -= nwl;
            memmove(buf, buf+nwl, inbuf);
        }
        after = buf + inbuf;
        room = BUFSIZE - inbuf;
    }

    /* The jobs pipe has closed -- prompting their removal */
    if ((nbytes < 0 && errno != EAGAIN) || nbytes == 0)
    {
        return -1;
    }
    return 0;
}
/*
 * Buffer and read a clients command and execute the given instructions. Cases
 * where client send invalid messages or one that lacks a network newline are
 * handled.If the command is invalid, the client is notified of the illegal 
 * action. If the command lacks a network newline, the command is considered
 * void and the client will have to resend the command. Valid commands are 
 * executed from here. This is will also notify if a clients connection goes
 * dark.
 *
 * @param client
 *        the client who sent the command
 * @param clientlist
 *        the list of currently active clients
 *
 * @return
 *        -1:            clients socket has closed, prompt for clients removal
 *         0:            command was executed
 */
int read_client(client_t *client, joblist_t *joblist)
{
    char buf[BUFSIZE+1];
    char *after = buf;
    int room = BUFSIZE;
    int inbuf = 0;
    int nbytes = 0;
    
    while ((nbytes = read(client->clientfd, after, room)) > 0)
    {
        inbuf += nbytes;
        int nwl;
    
        /* Only accept full commands */
        while ((nwl = find_network_newline(buf, inbuf)) > 0) 
        {
            buf[nwl -2] = '\0'; /* Remove \r\n */

            log_client_command(buf, client->clientfd);

            int validate = validate_command(buf);

            if (validate == -2) /* Error occurred */
            {
                return 0;
            }
            else if (validate == -1 || validate == 5) /* Invalid command */
            {
                if (write_client(INVALID_COMMAND, buf, client->clientfd) < 0
                    || write_client(NULL, CLIENT_WELCOME, client->clientfd) < 0)
                {
                    return -1;
                }
            }
            else if (validate == 0) /* Client requested command list */
            {
                if (write_commands(client->clientfd) < 0)
                {
                    return -1;
                }    
            }
            else
            {
                execute_command(buf, validate, client, joblist);
            }
            inbuf -= nwl;
            memmove(buf, buf+nwl, inbuf);
        }
        after = buf + inbuf;
        room = BUFSIZE- - inbuf;
    }

    /* The clients socket has closed -- prompting their removal */
    if ((nbytes < 0 && errno != EAGAIN) || nbytes == 0)
    {
        return -1;
    }
    return 0;
}

int main(void)
{
    /* Prepare for teardown signal */
    struct sigaction sig_handler;
    sigemptyset(&sig_handler.sa_mask);
    sig_handler.sa_handler = close_server_handler;
    sig_handler.sa_flags = 0;
    sigaction(SIGINT, &sig_handler, NULL);

    /* Set up server and sockets */
    struct sockaddr_in *self = init_server_addr(PORT);
    int listenfd = setup_server_socket(self, QUEUE_LENGTH);

    /* Set up structures to run server commands and connect clients */
    clientlist_t *clientlist = malloc(sizeof(struct clientlist));
    joblist_t *joblist = malloc(sizeof(struct joblist));
    connections_t *fdset = malloc(sizeof(struct connections));
    fdset->all_fds = malloc(sizeof(fd_set));

    if (fdset == NULL || fdset->all_fds == NULL || joblist == NULL 
        || clientlist == NULL)
    {
        perror("[SERVER] malloc");
        exit(1);
    }

    log_startup();

    /* Initialize structures and prepare the fdset for select() */
    fd_set listen_fds;
    FD_ZERO(fdset->all_fds);
    FD_SET(listenfd, fdset->all_fds);
    fdset->maxfd = listenfd;

    clientlist->head = clientlist->end = NULL;
    clientlist->size = 0;
    joblist->size = 0;
    joblist->head = joblist->end = NULL;
    clientlist->fdset = joblist->fdset = fdset;

    while (active) /* SIGINT not received */
    {
        listen_fds = *fdset->all_fds;
        int nready = select(fdset->maxfd + 1, &listen_fds, NULL, NULL, NULL);

        if (nready < 0)
        {
            if (errno != EINTR) /* kill signal wasn't recieved */
            {
                perror("[SERVER] select");
            }
            active = 0;
        }
        
        /* Potiental client is attempting to connect */
        if (active && FD_ISSET(listenfd, &listen_fds))
        {
            setup_client(listenfd, clientlist);
        }
        
        client_t *client = clientlist->head;
        
        /*
         * Iterate through the connected clients and read their command if one
         * was sent. If the client closes their connection, remove them from
         * the fd_set.
           */
        while (client)
        {
            int client_closed = 0;

            /* Read from the client only if there is something to read */
            if (FD_ISSET(client->clientfd, &listen_fds))
            {
                /* Read from the client and determine if the connection closed*/
                if ((client_closed = read_client(client, joblist)) < 0)
                {
                    client_t *closed_client = client;
                    client = client->next;
                    close_client(closed_client, clientlist);
                }
            }
            if (client_closed == 0)
            {
                client = client->next;
            }
        }
    
        job_t *job = joblist->head;
    
        /*
         * Iterate through the running jobs and re-direct their output to all of
         * the jobs watchers. If a watcher closes connection, remove them from
         * the clientlist and fd_set. 
           */
        while (job)
        {
            int job_closed = 0;

            /* Read from the job only if there is something to read */
            if (FD_ISSET(job->jobpipe, &listen_fds))
            {
                /* Read from the client and determine if the connection closed*/
                if ((job_closed = read_write_job(job, joblist)) < 0)
                {
                    //job_t *job_ended = job;
                    job = job->next;
                    /* TODO Remove the job */
                }
            }
            if (job_closed == 0)
            {
                job = job->next;
            }
        }
    }

    /* Begin tearing down the server */
    free(self);
    close(listenfd);
    clear_clients(clientlist);
    clear_jobs(joblist);
    log_shutdown();
    return 0;
}
