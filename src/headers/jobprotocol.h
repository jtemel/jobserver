#ifndef JOBPROTOCOL_H
#define JOBPROTOCOL_H

#include "serverdata.h"
#include "serverlog.h"

/*******************************************************************************
 *                             Job Helpers                                     *
 ******************************************************************************/

/* Determine what command was sent */
int execute_command(char *buf, int validate, client_t *client, 
                    joblist_t *joblist);
int run_job(char *buf, client_t *client, joblist_t *joblist);

int job(int clientfd, joblist_t *joblist);
int job_exists(char *buf, int clientfd, joblist_t *joblist);
int kill_job(char *buf, int clientfd, joblist_t *joblist);
int watch_job(char *buf, client_t *client, joblist_t *joblist);

/* Building and running the job (used by "run" command) */
int arg_count(char *buf);
int build_job(int readfd, client_t *client, joblist_t *joblist);
int forward_job_output(int stdoutfd, int stderrfd, int writefd, int jpid);
int fill_argv(char *buf, char ***, int size);
void generate_job_and_manager(int writefd, char *argv[]);
void execute(int stdoutfd, int stderrfd, char *argv[]);

#endif /* JOBPROTOCOLG_H */
