#include "csapp.h"

/* ----------- Data Structures ----------- */
typedef struct                   // Represents a pool of connected descriptors
{
    int maxfd;                   // Largest descriptor in read_set
    fd_set read_set;             // Set of all active descriptors
    fd_set ready_set;            // Subset of descriptors ready for reading
    int nready;                  // Number of ready descriptors from select

    int maxi;                    // High water index into client array
    int clientfd[FD_SETSIZE];    // Set of active descriptors
    rio_t clientrio[FD_SETSIZE]; // Set of active read buffers
}pool;

typedef struct _item             // Contain Information of a stock item
{
    int ID;                      // Stock ID
    int left_stock;              // Remaining stocks
    int price;                   // Stock price
    struct _item *left, *right;  // Pointers for child nodes
}item;

typedef struct cmdline           // Contain information of command sent by a client
{
    char cmd[5];                 // Command
    int val[2];                  // {ID, number} 
}cmdline;

/* --------------------------------------- */

/* --------- Function Prototyes ---------- */

/*   Event-based server management   */
void init_pool(int listenfd, pool *p);
void add_client(int connfd, pool *p);
void check_clients(pool *p);

/*   Data Structure management   */
item* findID(int ID);
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
item* root = NULL;               // Points root node
FILE *fp;                        // File pointer for data I/O
/* --------------------------------------- */

/* $begin echoserverimain */
int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */
    static pool pool;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }
    
    // Read saved data from stock.txt
    if(read_data() == -1)
        unix_error("Read from data failed");

    Signal(SIGINT, sigint_handler);
    listenfd = Open_listenfd(argv[1]);

    init_pool(listenfd, &pool);
    while (1) 
    {
        // Wait for listening/connected descriptor(s) to become ready
        pool.ready_set = pool.read_set;
        pool.nready = Select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);

        // If listening descriptor ready, add new client to pool
        if (FD_ISSET(listenfd, &pool.ready_set))
        {
            clientlen = sizeof(struct sockaddr_storage);
            connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
            add_client(connfd, &pool);
            printf("\033[0;32mConnected\033[0m to (%s, %s)\n", client_hostname, client_port);
        }
        
        check_clients(&pool);
    }
}
/* $end echoserverimain */

void init_pool(int listenfd, pool *p)
{
    // Initially, there are no connected desriptors
    int i;
    p -> maxi = -1;
    for (i = 0; i < FD_SETSIZE; i++)
        p -> clientfd[i] = -1;

    // Initially, listenfd is only member of select read set
    p->maxfd = listenfd;
    FD_ZERO(&p -> read_set);
    FD_SET(listenfd, &p -> read_set);
}

void add_client(int connfd, pool *p)
{
    int i;
    p -> nready--;
    for (i = 0; i < FD_SETSIZE; i++) // Find an available slot
    {
        if (p -> clientfd[i] < 0)
        {
            // Add connected descriptor to the pool
            p -> clientfd[i] = connfd;
            Rio_readinitb(&p -> clientrio[i], connfd);

            // Add the descriptor to the descriptor set
            FD_SET(connfd, &p -> read_set);

            // Update max descriptor and pool high water mark
            if (connfd > p -> maxfd)
                p -> maxfd = connfd;
            if (i > p -> maxi)
                p -> maxi = i;
            break;
        }
    }
    if (i == FD_SETSIZE)
        app_error("add_client error: Too many clients");

    return;
}

void check_clients(pool *p)
{
    int i, connfd, n;
    char buf[MAXLINE];
    cmdline argv;
    rio_t rio;

    for (i = 0; (i <= p->maxi) && (p -> nready > 0); i++)
    {
        connfd = p -> clientfd[i];
        rio = p -> clientrio[i];

        // If the descriptor is ready, echo a text line from it
        if ((connfd > 0) && (FD_ISSET(connfd, &p -> ready_set)))
        {
            p -> nready--;
            if ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
            {
                 printf("Server received %d bytes\n", n);
                 parseline(buf, &argv, n);
                 if (run_cmd(connfd, argv))
                 {
                    Close(connfd);
                    FD_CLR(connfd, &p -> read_set);
                    p -> clientfd[i] = -1;
                 }
            }
            // EOF detected, remove descriptor from pool
            else
            {
                Close(connfd);
                FD_CLR(connfd, &p -> read_set);
                p -> clientfd[i] = -1;
            }
        }
    }
}

// Search the tree for stock which its id == ID
// Return its address index if found, NULL if not
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
        sprintf(line, "%d %d %d\n", ptr -> ID, ptr -> left_stock, ptr -> price);
        strcat(str, line);
        write_data(ptr -> left, str);
        write_data(ptr -> right, str);
    }
}


// Convert a command line sent by a client
// into a form operatable by the server
void parseline(char *buf, cmdline *argv, int n)
{
    int argc = 0, temp = 0, c = 0;
    
    for (int i = 0; i < n; i++)
    {
        if (buf[i] == ' ' || buf[i] == '\n')
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
                temp = 10 * temp + (buf[i] - '0');
        }
    }
}

// Input command and run it
int run_cmd(int connfd, cmdline argv)
{
    item *ptr;

    if (!strcmp("show", argv.cmd))
    {
        char line[MAXLINE];
        line[0] = '\0';
        write_data(root, line);
        Rio_writen(connfd, line, MAXLINE);
    }
    else if (!strcmp("exit", argv.cmd))
        return 1;
    else if (!strcmp("buy", argv.cmd))
    {
        ptr = findID(argv.val[0]);
        if (!ptr)
            unix_error("Stock not in list");
        if (ptr -> left_stock < argv.val[1])
            Rio_writen(connfd, "Not enough left stock\n", MAXLINE);
        else
        {
            ptr -> left_stock -= argv.val[1];
            Rio_writen(connfd, "[buy] \033[0;32msuccess\033[0m\n", MAXLINE);
        }
    }
    else
    {
        ptr = findID(argv.val[0]);
        if (!ptr)
            unix_error("Stock not in list");
        ptr -> left_stock += argv.val[1];
        Rio_writen(connfd, "[sell] \033[0;32msuccess\033[0m\n", MAXLINE);
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
