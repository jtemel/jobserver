#ifndef SERVERLOG_H
#define SERVERLOG_H

/* No lines or paths may exceed the BUFSIZE below */
#define BUFSIZE 256

/*
 *
 */
#define JOB_EMPTY "[SERVER] No currently running jobs\r\n"
#define JOB_OVERLOAD "[SERVER] MAXJOBS exceeded\r\n"
#define SERVER_SHUTDOWN "[SERVER] Shutting down\r\n"
#define CLIENT_CLOSED "[CLIENT %d] Connection closed\r\n"
#define CLIENT_ERROR "[SERVER] Could not accept client\n"
#define CLIENT_ACCPT "[SERVER] Connection accepted\r\n"
#define CLIENT_WELCOME "[SERVER] Type \"commands\" to view valid commands\r\n"
#define WATCHING_JOB "[SERVER] Watching job %d\r\n"
#define END_WATCHING_JOB "[SERVER] No longer watching job %d\r\n"
#define CREATE_JOB "[SERVER] Job %d created\r\n"
#define JOB_EXIT "[JOB %d] Exited with status %d\r\n"
#define JOB_SIGNAL "[JOB %d] Exited due to signal\r\n"
#define JOB_STDOUT "[JOB %d] %s\r\n"
#define JOB_STDERR "*(JOB %d)* %s\r\n"
#define JOB_NOT_FOUND "[SERVER] Job %d not found\r\n"
#define INVALID_COMMAND "[SERVER] Invalid command: %s\r\n"
#define CLIENT_CMD "[CLIENT %d] %s\r\n"
#define JOB_LIST "[SERVER (%d)]%s\r\n" 

#define SERVER_ACT "[SERVER] Activated: %s\n"
#define SERVER_DEACT "[SERVER] De-activated: %s\n"

#define VALID_CMDS_S 5

/* List of valid commands */
extern char *cmdheads[VALID_CMDS_S];
extern char *cmdmsg[VALID_CMDS_S];
extern int indent[VALID_CMDS_S];
/*******************************************************************************
 *                              Server Log                                     *
 ******************************************************************************/
void log_message(char *buf);
void log_client_command(char *buf, int clientfd);

void log_startup();
void log_shutdown();

/*******************************************************************************
 *                      Server to Client Communication                         *
 ******************************************************************************/
int write_client(char *format, char *buf, int clientfd);
int write_job(char *format, pid_t jobpid, pid_t exit_status, 
                char *buf, int writefd);
int write_commands(int clientfd);

#endif /* SERVERLOG_H */
