/*
 * proxy.c
 *
 * Weikun Yang
 * guest511
 *
 * A multithreaded web proxy server.
 * 
 * 
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/in.h>

/* max listen queue */
#define LISTEN_QUEUE 1024

#define BUF_LEN (8192)
#define MED_BUF (256)
#define MAXLINE (8192)
#define MAX_CACHE_SIZE (20*1024*1024)
#define MAX_OBJECT_SIZE (100*1024)
#define MAX_THREAD (50)

#define DEFAULT_USERAGENT "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n"
#define DEFAULT_ACCEPT  "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
#define DEFAULT_ACCEPT_ENC "Accept-Encoding: gzip, deflate\r\n"
#define DEFAULT_CONNECTION "Connection: close\r\nProxy-Connection: close\r\n"

/* structure for buffered reading */
typedef struct{
    int fd;
    int read;
    char buf[BUF_LEN];
    char *bufp;
} read_t;

/* structure for cached objects
 * used in double linked list
 */
struct cache_node{
    char *data;
    struct cache_node *next, *prev;
    int size;
    char uri[MAXLINE];
};
typedef struct cache_node cache_t;

/* cache_list points to the header, 
 * cache_start points to the first cache_t in use, 
 * cache_lru points to the last cached_t in use, 
 * of course the least recently used one.
 */
cache_t *cache_list, *cache_start, *cache_lru;

/* total size of cached objects */
int cached_total;

/* structure representing a client request 
 * used in a queue
 */
typedef struct{
    struct sockaddr_in addr;
    int fd;
} client_req_t;

client_req_t req_queue[MAX_THREAD];
int queue_head, queue_tail;


/*struct idle_worker{
    pthread_t tid;
    struct idle_worker *next, *prev;
};
typedef struct idle_worker idle_worker_t;*/

/* mutex for gethostbyname */
pthread_mutex_t dns_lock = PTHREAD_MUTEX_INITIALIZER;

/* mutex locking the req_queue */
pthread_mutex_t req_lock = PTHREAD_MUTEX_INITIALIZER;

/* rwlock locking the cache list */
pthread_rwlock_t cache_lock;

/* semaphore representing unallocated requests */
sem_t req_sem;
/* semaphore representing idle worker threads */
sem_t idle_sem;

//int global_fd;
//struct sockaddr_in global_addr;

/*
 * Function prototypes
 */
void sigint_handler(int signum);
int parse_uri(char *uri, char *hostname, char *pathname, unsigned int *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
int open_listen_socket(unsigned short port);
int open_client_socket(char *hostname, unsigned short port);
int print_chars_ascii(char *str);
void *worker(void *data);
void process_req(int req_fd, struct sockaddr_in* addr, char *buffer);
void iptostr(uint32_t ipv4, char* str);
ssize_t force_write(int fd, void *user_buf, size_t n);
void init_buf_read(read_t* read_p, int fd);
ssize_t buf_read(read_t* read_p, void *user_buf, size_t n);
ssize_t buf_read_line(read_t* read_p, void *user_buf, size_t maxlen);
void quit_with_error(const char *msg);
void show_error(const char *msg);
/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
    int port, listen_fd, local_fd, i;
    socklen_t client_len;
    struct sockaddr_in local_addr;
    pthread_t tid;
    /* Check arguments */
    if (argc != 2) {
	   fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
	   exit(0);
    }
    port = atoi(argv[1]);
    if (port < 1024 || port > 65535){
        printf("try a port within [1024, 65536)!\n");
        exit(0);
    }
    /* initialise these unnamed semaphores */
    sem_init(&idle_sem, 0, 0);
    sem_init(&req_sem, 0, 0);

    /* always ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);
    //signal(SIGINT, sigint_handler);

    //idle_sem = sem_open(IDLE_SEM_NAME, O_CREAT | O_EXCL);
    //req_sem = sem_open(REQ_SEM_NAME, O_CREAT | O_EXCL);

    /* initialise request queue */
    queue_head = queue_tail = 0;

    /* initilise cache list */
    cache_list = (cache_t*)malloc(sizeof(cache_t));
    cache_start = cache_list;
    cache_lru = NULL;
    memset(cache_list, 0, sizeof(cache_t));
    cached_total = 0;

    /* initialise rwlock for cache list */
    pthread_rwlock_init(&cache_lock, NULL);

    /* create worker threads */
    for (i = 0; i < MAX_THREAD; i++)
        if (pthread_create(&tid, NULL, worker, NULL) < 0)
            quit_with_error("failed creating threads");

    /* open a listening socket */
    listen_fd = open_listen_socket(port);
    client_len = sizeof(struct sockaddr_in);

    /* proxy main routine */
    while (1){
        /* accept a client request */
        local_fd = accept(listen_fd, (struct sockaddr*)&local_addr, &client_len);
        if (local_fd < 0){
            show_error("accept failed");
            continue;
        }
        /* wait until one idle thread appears */
        sem_wait(&idle_sem);
        
        /* lock for adding a request to queue */
        pthread_mutex_lock(&req_lock);
        req_queue[queue_tail].fd = local_fd;
        bcopy(&req_queue[queue_tail].addr, &local_addr, client_len);
        queue_tail = (queue_tail + 1) % MAX_THREAD;
        // printf("good, queue updated\n");
        /* unlock */
        pthread_mutex_unlock(&req_lock);
        
        /* post one requst for processing */
        sem_post(&req_sem);

        /*process_req(local_fd, &local_addr);
        bcopy(&client_addr, &(req->addr), client_len);
        while (pthread_create(&thread, NULL, process_req, req) < 0);
        process_req(req);*/
    }
    pthread_rwlock_destroy(&cache_lock);
    return 0;
}

/*
void sigint_handler(int signum){
    sem_unlink(IDLE_SEM_NAME);
    sem_unlink(REQ_SEM_NAME);
    exit(0);
}*/

/* worker - main routine of worker threads */
void *worker(void *data){
    /* fd and addr are "thread local" */
    int fd;
    struct sockaddr_in addr;
    char *buf;
    pthread_t tid = pthread_self();
    /* detach itself */
    pthread_detach(tid);

    /* buf is "thread local" too */
    buf = (char*)malloc(2 * MAX_OBJECT_SIZE);
    fprintf(stderr, "%lu thread initialised\n", tid);
    while (1){
        /* declare itself idle */
        sem_post(&idle_sem);
        
        /* obtain a request */
        sem_wait(&req_sem);
        fprintf(stderr, "%lu request obtained\n", tid);
        
        /* lock queue for picking a request from queue */
        pthread_mutex_lock(&req_lock);
        fd = req_queue[queue_head].fd;
        bcopy(&addr, &req_queue[queue_head].addr, sizeof(struct sockaddr_in));
        queue_head = (queue_head + 1) % MAX_THREAD;
        /* unlock */
        pthread_mutex_unlock(&req_lock);
        
        /* process request */
        process_req(fd, &addr, buf);
    }
    free(buf);
}

/* process_req - the function that actually process the request */
void process_req(int req_fd, struct sockaddr_in* addr, char *buffer){
    /* buffered read object */
    read_t reader;
    /* various buffers */
    char line_buf[MAXLINE*2], method[MED_BUF], uri[MAXLINE], protocol[MED_BUF],
        host[MED_BUF], path[MAXLINE], *bufp;
    /* port number of remote host */
    unsigned int port;
    /* various variables
     * uri_no_host indicates the request uses relative addressing, 
     * for the remote host must be obtained by reading "Host: xxx" header
     * no_accept, no_acceptencoding, no_useragent indicate missing headers
     * in the request. 
     */
    int remote_fd = 0, line_len, uri_no_host, no_accept = 1,
        no_acceptencoding = 1, no_useragent = 1, obj_sz = 0, too_large = 0;
    /* point to cache_t */
    cache_t *cache_ptr, *next;
    /* initialise buffered read */
    init_buf_read(&reader, req_fd);

    iptostr(addr->sin_addr.s_addr, line_buf);
    fprintf(stderr, "%lu Connected to %s\n", pthread_self(), line_buf);
    /* read and parse the HTTP request */
    if (buf_read_line(&reader, line_buf, MAXLINE) <= 0 ||
        sscanf(line_buf, "%s %s %s", method, uri, protocol) != 3 ||
        strncasecmp(method, "GET", 3) != 0 ||
        (uri_no_host = parse_uri(uri, host, path, &port)) < 0
        )
        goto end;

    no_useragent = 1;
    no_acceptencoding = 1;
    no_accept = 1;
    /* store the rest of headers in buffer */
    bufp = buffer;
    while ((line_len = buf_read_line(&reader, bufp, MAXLINE))> 0){
        /* end of header */
        if (strcmp(bufp, "\r\n") == 0) break;
        /* "Host: xxx" header */
        if (strncasecmp(bufp, "Host:", 5) == 0){
            if (uri_no_host){
                strncpy(host, bufp + 6, MED_BUF);
                host[strlen(host) - 2] = '\0';
            }
            continue;
        }
        /* ignore Connection/Proxy-Connection/Keep-Alive */
        if (strncasecmp(bufp, "Connection:", 11) == 0 ||
            strncasecmp(bufp, "Proxy-Connection:", 17) == 0 ||
            strncasecmp(bufp, "Keep-Alive:", 11) == 0)
            continue;
        if (strncasecmp(bufp, "Accept:", 7) == 0)
            no_accept = 0;
        if (strncasecmp(bufp, "Accept-Encoding:", 16) == 0)
            no_acceptencoding = 0;
        if (strncasecmp(bufp, "User-Agent:", 11) == 0)
            no_useragent = 0;
        bufp += line_len;
    }
    if (line_len < 0){
        show_error("failed reading from client before end of HTTP request headers");
        goto end;
    }
    /* find the object in cache */
    snprintf(line_buf, MAXLINE, "http://%s:%d%s", host, port, path);
    /* lock for reading */
    pthread_rwlock_rdlock(&cache_lock);
    cache_ptr = cache_start->next;
    while (cache_ptr != NULL){
        if (strncmp(cache_ptr->uri, line_buf, MAXLINE) == 0)
            break;
        cache_ptr = cache_ptr->next;
    }
    if (cache_ptr != NULL){
        obj_sz = cache_ptr->size;
        /* copy contents to local buffer, reduce locking time */
        memcpy(buffer, cache_ptr->data, obj_sz);
        buffer[obj_sz] = '\0';
        fprintf(stderr, "reading from cache: %s\n", line_buf);
    }
    pthread_rwlock_unlock(&cache_lock);
    if (obj_sz > 0){
        /* return the contents to client */
        if (force_write(req_fd, buffer, obj_sz) < 0){
            show_error("writing to client failed");
            goto end;
        }
        /* lock for moving the cached object to front of list */
        pthread_rwlock_wrlock(&cache_lock);
        /* make sure it's still in the cache */
        if (cache_ptr->size > 0){
            /* delete node */
            next = cache_ptr->next;
            cache_ptr->prev->next = next;
            if (next != NULL)
                next->prev = cache_ptr->prev;
            /* insert at the start */
            next = cache_start->next;
            cache_start->next = cache_ptr;
            cache_ptr->prev = cache_start;
            cache_ptr->next = next;
            if (next != NULL)
                next->prev = cache_ptr;
        }
        pthread_rwlock_unlock(&cache_lock);
        goto end;
    }
    /* otherwise, read from remote host */
    /* modifiy the headers */
    line_len = snprintf(line_buf, MAXLINE, "GET %s HTTP/1.0\r\nHost: %s\r\n",
                        path, host);
    /* using client's accept/accept-encoding/user-agent unless not specified */
    if (no_accept)
        bufp += sprintf(bufp, "%s", DEFAULT_ACCEPT);
    if (no_acceptencoding)
        bufp += sprintf(bufp, "%s", DEFAULT_ACCEPT_ENC);
    if (no_useragent)
        bufp += sprintf(bufp, "%s", DEFAULT_USERAGENT);

    bufp += sprintf(bufp, "%s\r\n", DEFAULT_CONNECTION);

    /* initiate a connection to remote host */
    if ((remote_fd = open_client_socket(host, port)) < 0)
        goto end;
    /* send request headers */
    if (force_write(remote_fd, line_buf, line_len) < 0 ||
        force_write(remote_fd, buffer, bufp - buffer) < 0
        ){
        show_error("failed writing to remote host");
        goto end;
    }
    /* read from the remote host, buffer at most MAX_OBJECT_SIZE */
    bufp = buffer;
    while ((line_len = read(remote_fd, bufp,
            MAX_OBJECT_SIZE - (int)(bufp - buffer))) > 0){
        bufp += line_len;
        if (bufp - buffer >= MAX_OBJECT_SIZE){
            too_large = 1;
            break;
        }
    }
    if (line_len < 0){
        show_error("failed reading from remote host");
        goto end;
    }
    if (force_write(req_fd, buffer, bufp - buffer) < 0){
        show_error("failed writing to client");
        goto end;
    }
    /* too large for buffer. keep reading */
    if (too_large){
        printf("keep reading(no cache)\n");
        while ((line_len = read(remote_fd, line_buf, MAXLINE - 1)) > 0){
            if (force_write(req_fd, line_buf, line_len) < 0){
                show_error("failed writing to client.");
                goto end;
            }
        }
        if (line_len < 0){
            show_error("failed reading from remote host");
            goto end;
        }
    }
    /* cache the object */
    else{
        /* obtain a write lock */
        pthread_rwlock_wrlock(&cache_lock);
        /* get a new cache obj */
        if (cache_start != cache_list){
            cache_ptr = cache_list;
            cache_list = cache_list->next;
            cache_list->prev = NULL;
        }else
            cache_ptr = (cache_t*)malloc(sizeof(cache_t));
        if (cache_lru == NULL)
            cache_lru = cache_ptr;
        /* insert the object at the begining of the list */
        cache_ptr->next = cache_start->next;
        if (cache_ptr->next != NULL)
            cache_ptr->next->prev = cache_ptr;
        cache_ptr->prev = cache_start;
        cache_start->next = cache_ptr;
        /* copy data into cache */
        cache_ptr->size = bufp - buffer;
        cache_ptr->data = (char*)malloc(cache_ptr->size + 1);
        memcpy(cache_ptr->data, buffer, cache_ptr->size);
        snprintf(cache_ptr->uri, MAXLINE, "http://%s:%d%s", host, port, path);
        cached_total += cache_ptr->size;
        /* eviction procedures */
        while (cached_total > MAX_CACHE_SIZE){
            /* remove lru obj */
            cache_ptr = cache_lru;
            if (cache_lru->prev == cache_start)
                cache_lru = NULL;
            else{
                cache_lru = cache_lru->prev;
                cache_lru->next = NULL;
            }
            free(cache_ptr->data);
            cached_total -= cache_ptr->size;
            cache_ptr->size = 0;
            cache_ptr->prev = NULL;
            cache_ptr->next = cache_list;
            cache_list->prev = cache_ptr;
            cache_list = cache_ptr;
        }
        pthread_rwlock_unlock(&cache_lock);
    }
end:
    if (remote_fd > 0)
        close(remote_fd);
    close(req_fd);
}

/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, unsigned int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) == 0)
        hostbegin = uri + 7;
    else
        hostbegin = uri;
    /* Extract the host name */
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')
        *port = atoi(hostend + 1);

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL)
        return -1;
    strcpy(pathname, pathbegin);
    return (len == 0)? 1: 0;
}


/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
}

inline void iptostr(uint32_t ipv4, char* str){
    uint32_t host = ntohl(ipv4);
    unsigned char a, b, c, d;
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;
    sprintf(str, "%u.%u.%u.%u", a, b, c, d);
}

/* listen on a local socket */
int open_listen_socket(unsigned short port){
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0), opt_val = 1;
    struct sockaddr_in server_addr;
    if (listen_fd < 0)
        quit_with_error("failed opening socket");
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const void*)&opt_val,
            sizeof(int)) < 0)
        quit_with_error("setsockopt failed");
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((unsigned short)port);
    
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        quit_with_error("bind failed");

    if (listen(listen_fd, LISTEN_QUEUE) < 0)
        quit_with_error("listend failed");
    return listen_fd;
}

/* connect to remote host */
int open_client_socket(char *hostname, unsigned short port){
    int client_fd;
    struct hostent *hostp;
    struct sockaddr_in server_addr;

    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        /* internal error */
        show_error("failed opening client socket");
        return client_fd;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    
    /* lock */
    pthread_mutex_lock(&dns_lock);
    if ((hostp = gethostbyname(hostname)) == NULL){
        /* dns error */
        show_error("failed resolving host");
        return -1;
    }
    bcopy(hostp->h_addr_list[0], &server_addr.sin_addr.s_addr, hostp->h_length);
    /* unlock */
    pthread_mutex_unlock(&dns_lock);

    server_addr.sin_port = htons(port);
    server_addr.sin_family = AF_INET;
    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        /* remote error */
        show_error("failed connecting to host");
        return -1;
    }
    return client_fd;
}

/* write n bytes of data, unless error occurs */
ssize_t force_write(int fd, void *user_buf, size_t n){
    int left = n, written;
    char *bufp = (char *)user_buf;
    while (left > 0){
        if ((written = write(fd, bufp, left)) <= 0){
            if (errno != EINTR)
                return -1;
            written = 0;
        }
        left -= written;
        bufp += written;
    }
    return n;
}

void init_buf_read(read_t* read_p, int read_fd){
    read_p->fd = read_fd;
    read_p->read = 0;
    read_p->bufp = read_p->buf;
}

/* buffered read, always read n bytes */
ssize_t buf_read(read_t* read_p, void *user_buf, size_t n){
    int cnt;
    while (read_p->read <= 0){
        read_p->read = read(read_p->fd, read_p->buf, sizeof(read_p->buf));
        if (read_p->read < 0){
            if (errno != EINTR)
                return -1;
        }else if (read_p->read == 0)
            return 0;
        else
            read_p->bufp = read_p->buf;
    }
    cnt = n;
    if (n > read_p->read)
        cnt = read_p->read;
    memcpy(user_buf, read_p->bufp, cnt);
    read_p->bufp += cnt;
    read_p->read -= cnt;
    return cnt;
}

/* read one line */
ssize_t buf_read_line(read_t* read_p, void *user_buf, size_t maxlen){
    int i, cnt;
    char c, *bufp = (char *)user_buf;
    for (i = 1; i < maxlen; i++){
        cnt = buf_read(read_p, &c, 1);
        if (cnt == 1){
            *bufp = c;
            bufp++;
            if (c == '\n') break;
        }
        else if (cnt == 0){
            if (i == 1) return 0;
            else break;
        }
        else return cnt;
    }
    *bufp = '\0';
    return i;
}

int print_chars_ascii(char *str){
    int n = 0;
    char *ptr = str;
    while(*ptr){
        printf("0x%02d(%c)", *ptr, isalpha(*ptr)?*ptr:' ');
        ptr++;
        n++;
    }
    putchar('\n');
    return n;
}


/* print error message (using errno) */
void show_error(const char *msg){
    fprintf(stderr, "%s : %s\n", msg, strerror(errno));
}

/* print error message then quit */
void quit_with_error(const char *msg){
    show_error(msg);
    exit(0);
}

