#ifndef JOBCOMMANDS_H
#define JOBCOMMANDS_H

#ifndef PORT
  #define PORT 50000
#endif

#ifndef CLIENT_CMDS_S
	#define CLIENT_CMDS_S 7
#endif

/* No lines or paths may exceed the BUFSIZE below */
#define BUFSIZE 256

/* List of valid commands the client can send */
extern const char *client_cmds[CLIENT_CMDS_S];

/*******************************************************************************
 *                         Message Transmission Helpers                        *
 ******************************************************************************/
int validate_command(char *buf);
int find_network_newline(char *buf, int inbuf);

#endif /* JOBCOMMANDS_H */
