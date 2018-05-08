#ifndef SERVERDATA_H
#define SERVERDATA_H

#ifndef MAX_JOBS
    #define MAX_JOBS 32
#endif

/*******************************************************************************
 *                          Communincation Structure                           *
 ******************************************************************************/
typedef struct connections
{
    fd_set *all_fds;
    int maxfd;

} connections_t;

/*******************************************************************************
 *                            Client Structures                                   *
 ******************************************************************************/

/*
 * Store a client that has connected to the server.
 *
 * @data clientfd
 *        the clients file descriptor to communicate through
 * @data next
 *        point the next client connected to the server
 * @data prev
 *        point the previous client connected to the server
 */
typedef struct client
{
    int clientfd;
    struct client *next;
    struct client *prev;

} client_t;

/*
 * Store a list of clients that are currently connect to the server. This list
 * is not bounded (other than by the fd_set size of per OS). Clients that are no
 * longer active should be removed from the clientlist as soon as the 
 * disconnection happens.
 *
 * @data head
 *        the first client connected to the server
 * @data end
 *        the last client connected to the server (can be the same as head)
 * @data size
 *        the total count of actively connected clients
 * @data fdset
 *        the fd_set that holds all concurrent connections to the server (which 
 *        holds every connected clients clientfd).
 */
typedef struct clientlist
{
    client_t *head;
    client_t *end;
    size_t size;
    connections_t *fdset;

} clientlist_t;


/*******************************************************************************
 *                            Job Watcher Structures                               *
 ******************************************************************************/
typedef struct watcher
{
    client_t *client;
    struct watcher *next;
    struct watcher *prev;
    
} watcher_t;

/*
 * Store a list of clients that are currently watching a job. The amount of 
 * clients watching a job is not bounded, thus the size data is used strictly
 * for easier appending or removale of watchers.
 *
 * @data head
 *        the first active client watching the job
 * @data end
 *        the last active client watching the job (can be the same as head)
 * @data size
 *        the total count of active clients watching the job
 */
typedef struct watchlist
{
    watcher_t *head;
    watcher_t *end;
    size_t size;

} watchlist_t;

/*******************************************************************************
 *                                Job Structures                                   *
 ******************************************************************************/

/*
 * Store a job that the server initialized. Communication between the server and
 * the job will be done through the corresponding job struct. Non-active jobs
 * will have their job struct removed.
 *
 * @data pid
 *        the process id of the running job
 * @data mpid
 *        the process id of the job manager
 * @data jobpipe
 *        the pipe that communication between the server and job manager happens
 * @data watcherslist
 *        the list of clients watching the job
 * @data next
 *        the next job running on the server
 * @data prev
 *        the prev job running on the server
 */
typedef struct job
{
    pid_t pid;
    pid_t mpid;
    int jobpipe;
    watchlist_t *watchlist;
    struct job *next;
    struct job *prev;

} job_t;

/*
 * Store a list of jobs that the server has initialized. The list is bounded
 * by MAX_JOBS, thus attempting to run a job that exceeds the MAX_JOB bound
 * will be ignored and reported.
 *
 * @data head
 *        the first job running on the server
 * @data end
 *        the last job running the server (can be the same as head)
 * @data size
 *        the total count of actively running jobs
 * @data fdset
 *        the fd_set that holds all concurrent connections to the server (which 
 *        holds every running jobs pipe).
 */
typedef struct joblist
{
    job_t *head;
    job_t *end;
    size_t size;
    connections_t *fdset;

} joblist_t;

/*============================================================================*/

/*******************************************************************************
 *                          Communincation Helpers                             *
 ******************************************************************************/
void add_fd(int fd, connections_t *connections);
void close_fd(int fd, connections_t *connections);

/*******************************************************************************
 *                              Client Helpers                                 *
 ******************************************************************************/

int add_client(int clientfd, clientlist_t *clientlist);
void close_client(client_t *client, clientlist_t *clientlist);
void free_client(client_t *client);
void clear_clients(clientlist_t *clientlist);

/*******************************************************************************
 *                              Job Helpers                                     *
 ******************************************************************************/

int add_job(pid_t pid, pid_t mpid, int jobpipe, client_t *client, 
            joblist_t *joblist);
int remove_job(pid_t pid, joblist_t *joblist);
job_t *find_job(pid_t pid, joblist_t *joblist);
int jobcmp(job_t *job1, job_t *job2);
void free_job(job_t *job);
void clear_jobs(joblist_t *joblist);

/*******************************************************************************
 *                         Job Watcher Helpers                                 *
 ******************************************************************************/

int add_watcher(pid_t pid, client_t *client, joblist_t *joblist);
void remove_watcher(watcher_t *watcher, watchlist_t *watchlist);
watcher_t *find_watcher(client_t *client, watchlist_t *watchlist);
int watchercmp(watcher_t *watcher1, watcher_t *watcher2);


#endif /* SERVERDATA_H */
