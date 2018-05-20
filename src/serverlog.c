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

#include "headers/serverdata.h"
#include "headers/jobcommands.h"
#include "headers/serverlog.h"

/* Separator for the server.log to differentiate between startups */
char *separator = "=======================================================";

/* The servers log */
static FILE *serverlog;

/*******************************************************************************
 *                          Display Valid Commands                             *
 ******************************************************************************/
/*
 * The list of valid commands the server accepts. This is formatted to be sent
 * to a client who requests the valid commands.
 */
char *cmdheads[] =
{
    "[SERVER] List of Valid Commands:",
    "[SERVER] jobs:",
    "[SERVER] joblist:",
    "[SERVER] run [jobname] [args]:",
    "[SERVER] watch [pid]:",
    "[SERVER] kill [pid]:",
    "[SERVER] exit:"
};

/*
 * Appended to the cmdheads, in respected order.
 */
char *cmdmsg[] =
{
    "\r\n",
    "list the currently running jobs\r\n",
    "list the jobs that can be run\r\n",
    "run a new job \"jobname\" with arguments (0+ args)\r\n",
    "watch the job specified by pid's output\r\n",
    "kill the job specified by pid\r\n",
    "close your connection with the server\r\n"
};

/*
 * Indent amount between the cmdhead[i] and cmdmsg[i], to ensure corect format.
 */
int cmdindent[] = { 0, 18, 15, 2, 11, 12, 18 };


/*******************************************************************************
 *                           Display Valid Jobs                                *
 ******************************************************************************/
/*
 * The list of valid jobs the server can run. This is formatted to be sent
 * to a client who requests the valid joblist.
 */
char *jobheads[] =
{
    "[SERVER] List of available jobs:",
    "[SERVER] randprint [n > 0]:\n",
    "[SERVER] pfact [n > 0]:\n",
    "[SERVER] print_ptree [PID]:\n"
};

/*
 * Appended to the jobheads, in respected order.
 */
char *jobmsg[] =
{
    "\r\n",
    "print \"A stitch in time\" n times, in randomly sized pieces\r\n",
    "use sieve factorization to find exactly two primes that factor n\r\n",
    "print the /proc/ tree rooted at PID\r\n"
};

/*
 * Indent amount between the jobhead[i] and jobmsg[i], to ensure corect format.
 */
int jobindent[] = { 0, 9, 9, 9 };


/*******************************************************************************
 *                                Server Log                                   *
 ******************************************************************************/

/*
 * Log the message to the servers stdout as well as the active server.log.
 * Network newlines (if applicable) are not stripped before logging.
 *
 * @param buf
 *        the message to log
 */
void log_message(char *buf)
{
    printf("%s", buf);
    fprintf(serverlog, "%s", buf);
}

/*
 * Log the command the client sent to the server, regardless of if its valid
 * or not.
 *
 * @param buf
 *        the command the client requested
 * @param clientfd
 *        wwthe fd of which the command came
 */
void log_client_command(char *buf, int clientfd)
{
    char msg[BUFSIZE + 1];
    if (sprintf(msg, CLIENT_CMD, clientfd, buf) < 0)
    {
        return;
    }
    log_message(msg);
}

/*
 * Open the server.log file (if it doesn't exist, make one) and log the servers
 * start up time to stdout and the server.log.
 *
 * @exit
 *        1:            server.log could not be oppened (this is pre-mallocs)
 */
void log_startup()
{
    serverlog = fopen("../server.log", "ab");
    if (serverlog == NULL)
    {
        fprintf(stderr, "[SERVER] server.log error\n");
        exit(1);
    }

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char time[64];
    strftime(time, sizeof(time), "%c", tm);

    char buf[BUFSIZE+1];
    if (sprintf(buf, SERVER_ACT, time) < 0)
    {
        return;
    }

    fprintf(stdout, "%s\n", separator);
    fprintf(stdout, "%s", buf);
    fprintf(serverlog, "%s\n", separator);
    fprintf(serverlog, "%s", buf);
}

/*
 * Log the time the server was de-actived to stdout and the server.log, closing
 * the server.log file before exiting.
 */
void log_shutdown()
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char time[64];
    strftime(time, sizeof(time), "%c", tm);

    char buf[BUFSIZE+1];
    if (sprintf(buf, SERVER_DEACT, time) < 0)
    {
        return;
    }

    fprintf(stdout, "%s", buf);
    fprintf(stdout, "%s\n", separator);
    fprintf(serverlog, "%s", buf);
    fprintf(serverlog, "%s\n", separator);
    fclose(serverlog);
}

/*******************************************************************************
 *                      Server to Client Communication                         *
 ******************************************************************************/

/*
 * Write to the client a message "buf" in the specified format "format". Format
 * parameter is NULL if the server is senting one of the following messages:
 *
 * 1. The connection accepted message
 * 2. The initial "welcome" message
 *
 * If the server has recieved an invalid command or requests the message to be
 * formatted, the format parameter will be set as such.
 *
 * All messages written will be logged.
 *
 * @param format
 *        one of the message formats defined in serverlog.h
 * @param buf
 *        the message the server is sending to the client
 * @param clientfd
 *        the client's fd to write the message too
 *
 * @return
 *        -1:           the clients has closed its socket
 *        0:            the messages were written to the client successfully
 *        1:            an error occured and the message could not be sent
 */
int write_client(char *format, char *buf, int clientfd)
{
    char msg[BUFSIZE + 1];
    if (format != NULL && buf != NULL)
    {
        if (sprintf(msg, format, buf) < 0)
        {
            return 1;
        }
    }
    else if (buf == NULL)
    {
        if (sprintf(msg, format, clientfd) < 0)
        {
            return 1;
        }
    }
    else
    {
        if (strcpy(msg, buf) < 0)
        {
            return 1;
        }
    }

    log_message(msg);
    if (write(clientfd, msg, strlen(msg)) != strlen(msg))
    {
        return -1;
    }
    return 0;
}

/*
 * Write to the server the output of the job it ahs created. The exit_status
 * parameter should be -1 if the job is sending one of the following messages:
 *
 * 1. The jobs stdout
 * 2. The jobs stderr
 * 3. The validation message of a successful job creation
 * 4. The jobs "killed by signal" message (command "kill [pid]")
 * 5. The client requested to watch the job (command "watch [pid]")
 * 6. The client requested to no longer watch the job (command "watch [pid]")
 *
 * If exit_status > 0, then the JOB_EXIT message should be the provied format.
 *
 * All messages written will be logged.
 *
 * @param format
 *        one of the job message formats defined in serverlog.h
 * @param jobpid
 *        the pid of the job requesting the message transfer
 * @param exit_status
 *         the jobs exit status (= -1 if void)
 * @param buf
 *        the message the job is sending to the client
 * @param writefd
 *        the pipe to write the jobs message too
 *
 * @return
 *        -1:           the write failed
 *        0:            the messages were written to the client successfully
 *        1:            an error occured and the message could not be sent
 */
int write_job(char *format, pid_t jobpid, int exit_status,
                char *buf, int writefd)
{
    char msg[BUFSIZE + 1];
    if (exit_status >= 0)
    {
        if (sprintf(msg, format, jobpid, exit_status) < 0)
            return 1;

        if (write(writefd, msg, strlen(msg)) != strlen(msg))
            return -1;

        return 0;
    }
    if (buf == NULL)
        buf = "";

    if (sprintf(msg, format, jobpid, buf) < 0)
        return 1;


    if (write(writefd, msg, strlen(msg)) != strlen(msg))
        return -1;

    return 0;
}

/*
 * Distribute the output of the job to all the watchers. If the output could
 * not be written to a client, remove the client from the watchlist. This occurs
 * when the client closes their connection to the server.
 * 
 * @param buf
 *      the output of the job to distribute
 * @param watchlist
 *      the list of clients that are watching the job to sent output too
 *
 * @return
 *      -1:         error occured and the output can not be sent
 *      0:          the output was sent to the jobs (or was attempted)
 */
int write_to_watchers(char *buf, watchlist_t *watchlist)
{
    char msg[BUFSIZE+1];
    if (sprintf(msg, "%s\r\n", buf) < 0)
        return -1;

    log_message(msg);

    watcher_t *watcher = watchlist->head;
    int skip = 0;
    while (watcher)
    {
        /* Client closed its connection */
        if ((skip = write(watcher->client->clientfd, msg, strlen(msg))) < 0)
        {   
            watcher_t *temp = watcher;
            watcher = watcher->next;
            remove_watcher(temp, watchlist);
        }
        if (skip >= 0)
            watcher = watcher->next;
    }
    return 0;
}

/*
 * Write to the client the list of valid commands or valid jobs that the server
 * can take. This should be called if the client sends the command "commands" or 
 * "joblist".
 *
 * @param clientfd
 *        the clients fd to write too
 * @param type
 *         0 for command list, else for job list
 *
 * All messages written will be logged to the servers stdout and server.log.
 *
 * @return
 *        -1:           the clients has closed its socket
 *        0:            the messages were written to the client successfully
 */
int write_setmsg(int clientfd, int type)
{
    char **head = cmdheads;
    int *indent = cmdindent;
    char **msg = cmdmsg;
    int bound = VALID_CMDS_S;

    if (type > 0)
    {
        head = jobheads;
        indent = jobindent;
        msg = jobmsg;
        bound = JOB_TOTAL;
    }

    for (int i = 0; i < bound; i++)
    {
        char cmd[BUFSIZE + 1];
        if (sprintf(cmd, "%s%*s%s", head[i], indent[i], "", msg[i]) < 0)
            continue;
        
        log_message(cmd);

        if (write(clientfd, cmd, strlen(cmd)) != strlen(cmd))
            return -1;
       
    }
    return 0;
}

/*
 * Notify the connected clients of the server's shutdown, prompting them to
 * terminate.
 */
void notify_clients_shutdown(clientlist_t *clientlist)
{
    /* Errors are void since the program is terminating */
    for(client_t *client = clientlist->head; client; client = client->next)
    {
        write(client->clientfd, SERVER_SHUTDOWN, strlen(SERVER_SHUTDOWN));
    }
}
