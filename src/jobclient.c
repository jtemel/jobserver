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

#define INVALID_COMMAND "Command not found\n"
#define COMMAND "%s\r\n"
#define CONNECTION_CLOSED "Connection closed\n"

int read_server(int readfd, int writefd)
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
            if (write(writefd, buf, inbuf) != strlen(buf))
            {
                perror("write");
                return 1;
            }
            inbuf -= nwl;
            memmove(buf, buf + nwl, inbuf);
        }
        after = buf + inbuf;
        room = BUFSIZE - inbuf;
    }

    if (nbytes < 0 && errno != EAGAIN)
    {
        perror("read");
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

    int soc = connect_to_server(PORT, argv[1]);

    int closed = 0;

    fd_set all_fds, listen_fds;
    FD_ZERO(&all_fds);
    FD_SET(soc, &all_fds);
    FD_SET(STDIN_FILENO, &all_fds);
    fcntl(soc, F_SETFL, O_NONBLOCK);

    while (!closed)
    {
        listen_fds = all_fds;

        int nready = select(soc + 1, &listen_fds, NULL, NULL, NULL);

        if (nready < 0)
        {
            perror("select");
            close(soc);
            exit(1);
        }

        if (soc > -1 && FD_ISSET(soc, &listen_fds))
        {
            //closed = read_from(soc, STDOUT_FILENO);
        }
        else
        {
            //closed = read_from(STDIN_FILENO, soc);
        }
    }

    printf("%s", CONNECTION_CLOSED);
    close(soc);

    return 0;
}
