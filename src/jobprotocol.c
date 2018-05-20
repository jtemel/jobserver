#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <regex.h>

#include "headers/serverdata.h"
#include "headers/serverlog.h"
#include "headers/jobcommands.h"
#include "headers/jobprotocol.h"

#ifndef JOBS_DIR
    #define JOBS_DIR "jobs/"
#endif

/*
 * Determine which of the commands ther server recieved and direct the flow to
 * execute the given command. Invalid commands are also filtered out.
 *
 * @param buf
 *        the command the server recieved and is attemting to execute
 * @param validate
 *        the int value of the command (the index if appears at in the cmds list)
 * @param client
 *        the client who invoked the command
 * @param joblist
 *        the list of currently running jobs to add/remove jobs to/from
 *
 * @return
 *        -1:         an invalid command was received or the command was unable
 *                    to be executed
 *         0:         the command was executed successfully
 *         1:         an error occured (lib or syscall function) and the command
 *                    was not executed
 */
int execute_command(char *buf, int validate, client_t *client, 
                    joblist_t *joblist)
{
    switch(validate)
    {
        case 1: /* display jobs */
            return job(client->clientfd, joblist);
        case 2: /* run job */
            return run_job(buf, client, joblist);
        case 3: /* kill */
            return kill_job(buf, client->clientfd, joblist);
        case 4: /* watch */
            return watch_job(buf, client, joblist);
    }
    return -1;
}
/*******************************************************************************
*                                Jobs Command                                  *
*******************************************************************************/
/*
 * Prepare a list of the active jobs running on the server and write it to the
 * client who requested it. If there are no jobs currently running, write the
 * appropiate message.
 *
 * @param clientfd
 *        the fd of the client who invoked the command
 * @param joblist
 *        the list of currently running jobs to parse
 *
 * @return
 *     Note the return value is determined by write_client in serverlog.c
 *
 *        -1:           the clients has closed its socket
 *        0:            the messages were written to the client successfully
 *        1:            an error occured and the message could not be sent
 */
int job(int clientfd, joblist_t *joblist)
{
    if (joblist->size == 0)
    {
        return write_client(NULL, JOB_EMPTY, clientfd);
    }
    
    char buf[BUFSIZE + 1];
    job_t *job = joblist->head;
    
    while (job)
    {
        sprintf(buf, "%s %d", buf, job->pid); /* error checking omitted since
                                                the error can be skipped */
        job = job->next;
    }
    
    return write_client(JOB_LIST, buf, clientfd);
}

/*******************************************************************************
*                                Kill Command                                  *
*******************************************************************************/

/*
 * Locate the job that the client wants to kill and kill it, then notify the
 * client. If the job doesn't exist, the appropiate message is written instead.
 *
 * @param buf
 *      the char representation of the job's pid to kill
 * @param clientfd
 *      the fd of the client who requested the kill
 * @param joblist
 *      the list of jobs to find the specific job within
 *
 * @return
 *      -1:         no job was found with the given pid
 *      0:          the job was killed successfully
 *
 */
int kill_job(char *buf, int clientfd, joblist_t *joblist)
{
    pid_t jpid;
    if ((jpid = job_exists(buf, clientfd, joblist)) > 1)
    {
        kill(jpid, SIGINT);
        return 0;
    }
    return -1;
}
/*******************************************************************************
*                                Watch Command                                 *
*******************************************************************************/

/*
 * Locate the job the client wants to watch and add the client to the list of
 * watchers. If the client was already watching the job, it will no longer be
 * watching.
 *
 * @param buf
 *      the pid of the job
 * @param client
 *      the client who invoked the command and will be appended as a watcher
 *      or removed
 * @param joblist
 *      the list of jobs to find the job within
 *
 * @return
 *      -1:         no job was found with the given pid
 *      0:          the job was killed successfully
 */
int watch_job(char *buf, client_t *client, joblist_t *joblist)
{
    pid_t jpid;
    if ((jpid = job_exists(buf, client->clientfd, joblist)) > 1) /* Job exists*/
    {
        printf("%d\n", add_watcher(jpid, client, joblist)); /* Errors => job not watched */
        return 0;
    }
    return -1;
}


/*
 * Determine if the job exists and is running on the server. If not, write
 * the appropiate message to teh client.
 *
 * @param buf
 *      the command contatining the pid of the job
 * @param clientfd
 *      the fd of the client who invoked the command
 * @param joblist
 *      the list of the jobs to scan through
 *
 * @return
 *      Note the return value is determined by write_job in serverlog.c
 *
 *        -1:           the write failed
 *        0:            the messages were written to the client successfully
 *        1:            an error occured and the message could not be sent
 *      jpid:           the pid of the job the client requested access too
 */
int job_exists(char *buf, int clientfd, joblist_t *joblist)
{
    char *ptr = strchr(buf, ' ');
    if (!ptr)
        return write_job(JOB_NOT_FOUND, 0, -1, NULL, clientfd);
    
    pid_t jpid = strtol(++ptr, NULL, 10);

    if (jpid == 0 || find_job(jpid, joblist) == NULL)
        return write_job(JOB_NOT_FOUND, jpid, -1, NULL, clientfd);
    return jpid;
}
/*******************************************************************************
*                              Run Command                                     *
*******************************************************************************/

/*
 * Parse the jobname and args, set-up job manager and pipe and prepare to launch
 * the job. Validity of the command is checked here.s
 *
 * @param buf 
 *      the command the user requested, to be parsed
 * @param client
 *      the client who invoked the command and will be set as the first watcher
 * @param joblist 
 *      the list to append the new job too, given it succeeds
 *
 * @return
 *      -1:         an error occurred and the job could not be created
 *      0:          the job was successfully created and appended
 *
 */
int run_job(char *buf, client_t *client, joblist_t *joblist)
{
    int size;
    if ((size = arg_count(buf)) == 1 || joblist->size >= MAX_JOBS) 
    {
        return -1;
    }

    char *arg;
    char args[size][BUFSIZE + 1];
    char *argv[size];
    int i = 0;

    arg = strtok(buf, " ");
    arg = strtok(NULL, " ");
    while (arg && i < size) /* Parse the buf to get jobname + args */
    {
        if (strcpy(args[i], arg) < 0)
        {
            return -1;
        }
        argv[i] = args[i];
        arg = strtok(NULL, " ");
        i++;
    }
    argv[i] = NULL; /* Null terminate for execvp */
    
    int fd[2];
    pid_t mpid;
    
    if (pipe(fd) < 0 || (mpid = fork()) < 0)
    {
        return -1;
    }

    if (mpid == 0)
    {
        close(fd[0]);
        generate_job_and_manager(fd[1], argv);
    }
    
    if (mpid > 0)
    {
        close(fd[1]);
        /* Job couldnt be created */
        if (build_job(fd[0], mpid, client, joblist) < 0) 
        {
            kill(SIGINT, mpid); /* Avoid stray processes */
            wait(NULL);
            return -1;
        }
    }
    return 0;
}

/*
 * Read from the pipe connecting the server (this) and the job manager to 
 * retrieve the job's pid (ususally the managers pid + 1). Once the pid is
 * retrieved, add the new job to the joblist and set the client as its first
 * watcher. If the read times out, then kill both the job manager and the job.
 *
 * @param readfd
 *        the read fd of the pipe connecting the job manager and the server
 * @param mpid
 *          the pid of the job manager's process
 * @param client
 *        the client who requested the job creation and thus the first watcher
 *         of the jobs output
 * @param joblist
 *        the list of currently running job to append th enew job too
 *
 * @return
 *        -1:         an error occured, the job was either not appended, the client
 *                    was not appended to the jobs watch list or a syscall failed.
 *                    No job will be created as such.
 *        0:          the job was created, appended, and watched successfully
 */
int build_job(int readfd, pid_t mpid, client_t *client, joblist_t *joblist)
{    
    pid_t jpid;
    
    /* Avoid stray/zombie process */
    if (read(readfd, &jpid, sizeof(int)) != sizeof(int)
        || add_job(jpid, mpid, readfd, client, joblist) < 0)
    {
        kill(mpid, SIGINT);
        waitpid(mpid, NULL, 0);
        return -1;
    }
    return 0;
}

/*
 * Setup both the jov manager and the job in seperate processes. The job manager
 * will first write the job processes pid to the server, then begin reading all
 * output generated by the job, redirecting it to the server in a valid format. 
 * The job process will prepare to send the jobs stdout and stderr to the job 
 * manager and finally execute the job executable.
 *
 * @param writefd
 *        the file descriptor that the job manager uses to communicate to the 
 *        server
 * @param argv
 *        the argument list to run the job, as required by execvp()
 *
 * @exit
 *        -1:         an error occured at any stage and the job and/or job 
 *                    manager could not be created
 */
void generate_job_and_manager(int writefd, char *argv[])
{
    int stdoutfd[2];
    int stderrfd[2];
    pid_t jpid;

    if (pipe(stdoutfd) < 0 || pipe(stderrfd) < 0 || (jpid = fork()) < 0)
    {
        exit(-1);
    }

    if (jpid == 0) /* Job to be process */
    {
        close(stdoutfd[0]);
        close(stderrfd[0]);
        close(writefd);
        execute(stdoutfd[1], stderrfd[1], argv);
    }
    
    if (jpid > 0) /* Job manager */
    {
        close(stdoutfd[1]);
        close(stderrfd[1]);

        if (write(writefd, &jpid, sizeof(int)) < 0) /*Inform server of jobs pid*/
        {
            kill(jpid, SIGINT);
            exit(-1);
        }    
        forward_job_output(stdoutfd[0], stderrfd[0], writefd, jpid);
    }
    exit(-1);    
}

/*
 * Read the jobs output, determine the correct prepend (JOB_STDOUT or JOB_STDERR)
 * and forward it through the jobs pipe to the server. If the job exits by from
 * a signal, notify the server. If the job exits normally, report the exit status
 * to the server. All messages read from the server are logged in both stdout
 * and the server.log. Once the job is finished, exit with status code 0.
 *
 * @param stdoutfd
 *        the read pipe connected to the jobs stdout
 * @param stderrfd
 *        the read pipe connected to the jobs stderr
 * @param writefd
 *        the write pipe connected to the server via the job struct
 * @param jpid
 *        the pid of the job that output is being forwarded from
 *
 * @exit
 *        1:              an error occured and the job must be killed to avoid
 *                        stray processes
 *        0:              the jobs output and exit status has been reported
 *                        successfully
 */
int forward_job_output(int stdoutfd, int stderrfd, int writefd, int jpid)
{
    char buf[BUFSIZE+1];
    char *after = buf;
    char *prepend;
    int inbuf, nbytes, status;
    int room = BUFSIZE;
    int fd[] = { stdoutfd, stderrfd };
    int maxfd = stdoutfd > stderrfd ? stdoutfd : stderrfd;    
    int done;

    /* Prepare to read both stdout and stderr */
    fd_set all_fds, listen_fds;
    FD_ZERO(&all_fds);
    FD_SET(fd[0], &all_fds);
    FD_SET(fd[1], &all_fds);

    /* Loop until the job has exited */
    while ((done = waitpid(jpid, &status, WNOHANG)) == 0)
    {
        listen_fds = all_fds;

        int nready = select(maxfd, &listen_fds, NULL, NULL, NULL);

        if (nready < 0)
        {
            perror("select");
        }
        for (int i = 0; i < 2; i++) /* Check for both stdout and stderr */
        {
            memset(buf, 0, sizeof(buf));
            inbuf = 0;
            after = buf;
            nbytes = 0;

            prepend = i == 0? JOB_STDOUT : JOB_STDERR;

            if (fd[i] > -1 && FD_ISSET(fd[i], &listen_fds))
            {
                while ((nbytes = read(fd[i], after, room)) > 0)
                {
                    inbuf += nbytes;
                    int nwl;
    
                    /* Only accept full commands */
                    while ((nwl = find_network_newline(buf, inbuf)) > 0) 
                    {
                        buf[nwl - 2] = '\0';
                        /* Sent output formatted to server */
                        if (write_job(prepend, jpid, -1, buf, writefd) < 0)
                        {
                            kill(jpid, SIGINT); /* Cant forward job */
                        }
                        inbuf -= nwl;
                        memmove(buf, buf+nwl, inbuf);
                    }
                    after = buf + inbuf;
                    room = BUFSIZE- - inbuf;
                }
            }
        }
    }

    if (WIFEXITED(status)) /* Job exited indepenendly */
    {
        int exit_status = WEXITSTATUS(status);
        write_job(JOB_EXIT, jpid, exit_status, NULL, writefd);
    }
    else /* Kill command was executed */
    {
        write_job(JOB_SIGNAL, jpid, -1, NULL, writefd);
    }
    close(writefd);
    close(fd[0]);
    close(fd[1]);
    exit(0);
}

/*
 * Replace the stdoutfd and stderrfd pipes with the jobs stdout and stderr and
 * execute the job. If the job was invalid, or any error occured, this will
 * notify the job manager to handle the case.
 *
 * @param stdoutfd stderrfd
 *        the two pipes connected to the job manager used to send the jobs stdout
 *        and stderr too
 *
 * @param argv
 *        an array of arguments for execvp(), with argv[0] set as the jobs name,
 *        and argv[length - 1] as NULL, as required by execvp().
 *
 * @exit
 *        -1:         the final stages of the job creation encountered an error,
 *                    a syscall or library function failed, or the job does not
 *                    exist
 */
void execute(int stdoutfd, int stderrfd, char *argv[])
{
    if (dup2(stdoutfd, fileno(stdout)) == -1
        || dup2(stderrfd, fileno(stderr)) == -1)
    {
        exit(-1);
    }

    close(stdoutfd);
    close(stderrfd);

    char job_exe[BUFSIZE + 1];
    if (sprintf(job_exe, "%s%s", JOBS_DIR, argv[0]) < 0)
    {
        exit(-1);
    }
    execvp(job_exe, argv);
    exit(-1);
}

/*
 * Find and return the total number of words in the given buf, each seperated
 * by a space. This is used to build an argv set for execvp().
 *
 * Example: "run randprint 1" return 3, "run randprint" returns 2
 *
 * @param buf
 *        the string to count args within
 *
 * @return
 *        1:            there were no words found
 *        count:        the total number of words found +1.    
 */    
int arg_count(char *buf)
{
    int count = 0;
    char ch = ' ';
    for (; *buf; count += (*buf++ == ch));
    return count++;
}

