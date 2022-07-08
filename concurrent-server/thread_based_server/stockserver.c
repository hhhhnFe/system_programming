#include "csapp.h"

#define NTHREADS 4
#define SBUFSIZE 16

/* ----------- Data Structures ----------- */

typedef struct                   // Buffer of clients
{
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
}sbuf_t;

typedef struct _item             // Contain information of a stock item
{
    int ID;                      // Stock ID
    int left_stock;              // Remaining stocks
    int price;                   // Stock price
    int readcnt;                 // For readers-writers problem
    sem_t w;                 
    sem_t mutex;                 // Mutex for correct modification of the stock 
    struct _item *left, *right;  // Pointers for child nodes
} item;

typedef struct cmdline           // Contain information of command sent by a client
{
    char cmd[5];                 // Command
    int val[2];                  // {ID, number} 
}cmdline;

/* --------------------------------------- */

/* --------- Function Prototyes ---------- */

/*   Master thread buffer management    */
void sbuf_init(sbuf_t *sp, int n);
void subf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);     

/*   Worker thread function   */
void *thread(void *vargp);

/*   Data Structure management   */
item *findID(int ID);
void insert(int ID, int left_stock, int price);
void free_tree(item *ptr);

/*   Data I/O   */
int read_data();
void write_data(item *ptr, char *str);

/*   Operation of command   */
void parseline(char *buf, cmdline *argv, int n);
int run_cmd(int connfd, cmdline argv);

/*   Installed SIGINT handler   */
void sigint_handler(int sig);
/* --------------------------------------- */


/* ---------- Global variables ----------- */
sbuf_t sbuf;                  // Shared buffer of connected descriptors
item *root = NULL;            // Points root node
FILE *fp;                     // File pointer for data I/O
/* --------------------------------------- */

/* $begin echoserverimain */
int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */ 
    pthread_t tid;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }

    // Read saved data from stock.txt
    if(read_data() == -1)
        unix_error("Read from data failed");

    Signal(SIGINT, sigint_handler);

    /* 
       Initialize shared buffer
       and create worker threads
    */
    sbuf_init(&sbuf, SBUFSIZE);
    for (int i = 0; i < NTHREADS; i++)
        Pthread_create(&tid, NULL, thread, NULL);

    /*
        < Code 106 ~ 114 >
        Server repeatedly accepts requested clients
        and inserts its descriptor to shared buffer
    */
    listenfd = Open_listenfd(argv[1]); 
    while (1) 
    {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("\033[0;32mConnected\033[0m to (%s, %s)\n", client_hostname, client_port);
        sbuf_insert(&sbuf, connfd);  
    }
    
    return 0;
}
/* $end echoserverimain */

// Worker thread function
void *thread(void *vargp)
{   
    int n;
    char buf[MAXLINE];
    rio_t rio;
    cmdline argv;

    Pthread_detach(pthread_self());
    while (1)
    {
        // Get connfd from buffer
        int connfd = sbuf_remove(&sbuf);
        Rio_readinitb(&rio, connfd);
        while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) // Loops until it meets EOF 
        {
            printf("Server received %d bytes\n", n);
            parseline(buf, &argv, n);
            if(run_cmd(connfd, argv)) // If client sent "exit"
                break;
        }
        Close(connfd); // Client disconnects to server
    }
}

void sbuf_init(sbuf_t *sp, int n)
{
    sp -> buf = Calloc(n, sizeof(int));
    sp -> n = n;
    sp -> front = sp -> rear = 0;
    Sem_init(&sp -> mutex, 0, 1);
    Sem_init(&sp -> slots, 0, n);
    Sem_init(&sp -> items, 0, 0);
}
void subf_deinit(sbuf_t *sp)
{
    Free(sp -> buf);
}
void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp -> slots);
    P(&sp -> mutex);
    sp -> buf[(++sp -> rear) % (sp -> n)] = item;
    V(&sp -> mutex);
    V(&sp -> items);
}
int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp -> items);
    P(&sp -> mutex);
    item = sp -> buf[(++sp -> front) % (sp -> n)];
    V(&sp -> mutex);
    V(&sp -> slots);
    return item;
}

// Search the tree for stock which its id == ID
// Return its address if found, NULL if not
item* findID(int ID)
{
    item *ptr = root;
    
    while (ptr)
    {
        if (ptr -> ID == ID)
            return ptr;
        else if (ptr -> ID < ID)
            ptr = ptr -> right;
        else
            ptr = ptr -> left;
    }

    return NULL;
}

int read_data()
{
    int ID, left_stock, price;

    fp = fopen("stock.txt", "r");

    if (fp == NULL)
        return -1; 
    
    while (fscanf(fp, "%d %d %d", &ID, &left_stock, &price) != EOF)
        insert(ID, left_stock, price);

    fclose(fp);
    return 0;
}

// Create Binary Search Tree
void insert(int ID, int left_stock, int price)
{
    item *stock, *ptr;

    stock = (item*)malloc(sizeof(item));
    stock -> ID = ID;
    stock -> left_stock = left_stock;
    stock -> price = price;
    stock -> readcnt = 0;
    Sem_init(&stock -> mutex, 0, 1);
    Sem_init(&stock -> w, 0, 1);
    stock -> left = NULL;
    stock -> right = NULL;

    if (root == NULL)
    {
        root = stock;
        return;
    }

    ptr = root;
    while (ptr)
    {
        if (ID < ptr -> ID)
        {
            if (ptr -> left == NULL)
            {
                ptr -> left = stock;
                return;
            }
            ptr = ptr -> left;
        }
        else
        {
            if (ptr -> right == NULL)
            {
                ptr -> right = stock;
                return;
            }
            ptr = ptr -> right;
        }
    }
}

// Traverse the tree
// and print requested data
void write_data(item *ptr, char *str)
{
    if (!str)
    {
        if (ptr)
        {
            fprintf(fp, "%d %d %d\n", ptr -> ID, ptr -> left_stock, ptr -> price);
            write_data(ptr -> left, str);
            write_data(ptr -> right, str);
        }
    }
    else if (ptr)
    {
        char line[MAXLINE];

        P(&ptr -> mutex);
        (ptr -> readcnt)++;
        if (ptr -> readcnt == 1)
            P(&ptr -> w);
        V(&ptr -> mutex);

        sprintf(line, "%d %d %d\n", ptr -> ID, ptr -> left_stock, ptr -> price);
        strcat(str, line);
        
        P(&ptr -> mutex);
        (ptr -> readcnt)--;
        if (ptr -> readcnt == 0)
            V(&ptr -> w);
        V(&ptr -> mutex);

        write_data(ptr -> left, str);
        write_data(ptr -> right, str);
    }
}

// Convert a command line sent by a client
// into a form operatable by the thread 
void parseline(char *buf, cmdline *argv, int n)
{
    int argc = 0, temp = 0, c = 0;
    
    for (int i = 0; i < n; i++)
    {
        if (buf[i] == ' ' || buf[i] == '\n') // Meets end of a word
        {
            if(!argc) 
                argv -> cmd[c] = '\0';
            else
                argv -> val[argc-1] = temp;
            temp = 0;
            argc++;
            continue;
        }
        switch (argc)
        {
            case 0:
                argv -> cmd[c++] = buf[i];
                break;
            default:
                temp = 10 * temp + (buf[i] - '0'); // Converts string to integer
        }
    }
}

// Input command and run it
int run_cmd(int connfd, cmdline argv)
{
    item *ptr;
    if (!strcmp("exit", argv.cmd))
        return 1;

    if (!strcmp("show", argv.cmd))
    {
        char line[MAXLINE];
        line[0] = '\0';
        write_data(root, line);
        Rio_writen(connfd, line, MAXLINE);
    }
    else if (!strcmp("buy", argv.cmd))
    {
        ptr = findID(argv.val[0]);
        if (!ptr)
            unix_error("Stock not in list");

        P(&ptr -> w);
        P(&ptr -> mutex);

        if (ptr -> left_stock < argv.val[1])
            Rio_writen(connfd, "Not enough left stock\n", MAXLINE);
        else
        {
            ptr -> left_stock -= argv.val[1];
            Rio_writen(connfd, "[buy] \033[0;32msuccess\033[0m\n", MAXLINE);
        }

        V(&ptr -> mutex);
        V(&ptr -> w);
    }
    else
    {
        ptr = findID(argv.val[0]);
        if (!ptr)
            unix_error("Stock not in list");
        
        P(&ptr -> w);
        P(&ptr -> mutex);

        ptr -> left_stock += argv.val[1];
        Rio_writen(connfd, "[sell] \033[0;32msuccess\033[0m\n", MAXLINE);

        V(&ptr -> mutex);
        V(&ptr -> w);
    }

    return 0;
}

// If SIGINT signal is sent,
// write modified data to stock.txt
// and free allocated memory
void sigint_handler(int sig)
{
    fp = fopen("stock.txt", "w");
    write_data(root, NULL);
    fclose(fp);

    free_tree(root);

    printf("\n");
    exit(0);
}

// Traverse the tree
// and free allocated memory
void free_tree(item *ptr)
{
    if (ptr)
    {
        free_tree(ptr -> left);
        free_tree(ptr -> right);
        free(ptr);
    }
}
