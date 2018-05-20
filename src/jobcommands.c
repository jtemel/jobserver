#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#include "headers/jobcommands.h"

/*
 * All possible commands that are valid.
 */
const char *client_cmds[] =
{
    "^commands$",
    "^jobs$",
    "^run (.+)( [0-9]*)*$",
    "^kill ([0-9]+)$",
    "^watch ([0-9]+)$",
    "^exit$",
    "^joblist$"
};

/*
 * Validate that the command the client received or sent to the server is one
 * of the accepted commands, in the correct format.
 *
 * @param buf
 *      the command the client or server received
 *
 * @return
 *      -2:     error occurred and thus the validity of the command is
 *              unknown
 *      -1:      the command was not a match
 *      i:      return the index of the regex in client_cmds that matched the
 *              command
 */
int validate_command(char *buf)
{
    int match;
    regex_t regex;
    for (int i = 0; i < CLIENT_CMDS_S; i++)
    {
        match = regcomp(&regex, client_cmds[i], REG_EXTENDED|REG_NOSUB);

        if (match != 0)
        {
            fprintf(stderr, "[SERVER] Regex could not be compiled\n");
            return -2;
        }
        
        match = regexec(&regex, buf, 0, NULL, 0);

        if (!match)
        {
            return i;
        }
        else if (match != REG_NOMATCH)
        {
            regerror(match, &regex, buf, sizeof(buf));
            fprintf(stderr, "[SERVER] Regex match failed for %s\n", buf);
            return -2;
        }
        regfree(&regex);
    }
    return -1;
}

/*
 * Locate the network newline (\r\n) in the given string up to
 * and including the first 'inbuf' characters. String parameters
 * may or may not be null terminated.
 * 
 * @param buf
 *      the string to locate the \r\n within
 * @param inbuf
 *      the amount of chars in buf
 * 
 * @return
 *      -1:     no network newline was found 
 *      i+2:    the index of the char directly after the
 *              network newline
 */
int find_network_newline(char *buf, int inbuf)
{
    for (int i = 0; i < inbuf - 1; i++)
    {
        if (*(buf+i) == '\r' && *(buf+i+1) == '\n')
        {
            return i + 2;
        }
    }
    return -1;
}





























