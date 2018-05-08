#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "headers/socket.h"
#include "headers/jobcommands.h"

#define COMMAND "%s\r\n"
#define CONNECTION_CLOSED "[CLIENT] Connection closed\n"

/*
 * Read the output from the server, stripping network newlines prior to 
 * displaying it. 
 *
 * @param readfd
 *          the socket to read the output from
 *
 * @return:
 *      0:          output was read and forwarded to the server successfully
 *      1:          a non-recoverable error occurred, prompting for termination
 */
int read_server(int readfd)
{
    char buf[BUFSIZE + 1];
    char *after = buf;
    int inbuf = 0;
    int room = BUFSIZE;
    int nbytes;

    while ((nbytes = read(readfd, after, room)) > 0)
    {
        inbuf += nbytes;

        int nwl;
        while ((nwl = find_network_newline(buf, inbuf)) > 0)
        {
            buf[nwl - 2] = '\0';
            printf("%s\n", buf);
            inbuf -= nwl;
            memmove(buf, buf + nwl, inbuf);
        }
        after = buf + inbuf;
        room = BUFSIZE - inbuf;
    }

    if (nbytes < 0 && errno != EAGAIN)
    {
        perror("[CLIENT] read");
        return 1;
    }
    return 0;
}

/*
 * Read the command from the user via STDIN. If the user issues the "exit" 
 * command, prompt for termination. Command is forwarded to the server, and
 * validity of commands are NOT checked here.
 *
 * @param writefd
 *          the socket to write the command to
 *
 * @return:
 *      0:          command was read and forwarded to the server successfully
 *      1:          a non-recoverable error occurred, prompting for termination
 */
int read_command(int writefd)
{
   char buf[BUFSIZE + 1];
   int num_read = read(STDIN_FILENO, buf, BUFSIZE);

	if (num_read < 0)
	{
		perror("[CLIENT] read");	
		return 1;
	}		

	if (num_read == 0)
		return 0;

	buf[num_read - 1] = '\0';

	int validate = validate_command(buf);

	if (validate == 5) //Exit
		return 1;

    char msg[BUFSIZE + 4];
    if (sprintf(msg, COMMAND, buf) < 0)
    {
        perror("[CLIENT] sprintf");
        return 1;   
    }

	if (write(writefd, msg, strlen(msg)) < 0)
	{
		perror("[CLIENT] write");
		return 1;
	}
	return 0;

}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage:\n\tjobclient hostname\n");
        exit(1);
    }

    // Set-up socket
    int soc = connect_to_server(PORT, argv[1]);

    int closed = 0;

    // Prepare to lsiten to both STDIN and the socket
    fd_set all_fds, listen_fds;
    FD_ZERO(&all_fds);
    FD_SET(soc, &all_fds);
    FD_SET(STDIN_FILENO, &all_fds);
    fcntl(soc, F_SETFL, O_NONBLOCK);

    /*
     * Continously check for server output or a command request from the user.
     * Non-recoverable errors will terminate the loop
     */
    while (!closed)
    {
        listen_fds = all_fds;

        int nready = select(soc + 1, &listen_fds, NULL, NULL, NULL);

        if (nready < 0)
        {
            perror("[CLIENT] select");
            close(soc);
            exit(1);
        }

        if (soc > -1 && FD_ISSET(soc, &listen_fds)) //Server
        {
            closed = read_server(soc);
        }
        else // User
        {
            closed = read_command(soc);
        }
    }

    printf("%s", CONNECTION_CLOSED);
    close(soc);

    return 0;
}
