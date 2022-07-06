///////////////////////////////////
/*                               */
/* CSE4100-02                    */
/*                               */
/*  Project 1                    */
/*     - MyShell                 */
/*        - Phase 1&2&3          */
/*                               */
/*  Specification                */
/*    - Implemented my own shell */
/*      using c code             */
/*                               */
/*    - Support pipelining       */
/*                               */
/*    - Support FG/BG jobs       */
/*                               */
/*                               */
/*           20181621 H.C.KIM    */
/*                               */
///////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <setjmp.h>

#define MAXARGS   128
#define MAXLINE  8192
#define FG 0
#define BG 1
#define RUN 0
#define STP 1
#define END 2

// External variables
extern int h_errno;
extern char **environ;

/* ------ Data Structures ------- */
typedef void handler_t(int);

typedef struct process
{
    struct process *next;
    pid_t pid;
    char flag; // Whether process is running, stopped or ended
}process;

typedef struct job
{
    struct process *plist; // pid list of its pgid
    int j_idx;
    pid_t pgid;
    char flag;  // Whether job is running, stopped or ended
    char state; // Wheter job is foreground or background
    char *cmd_line;
    struct job *next;
    struct termios tmodes;
}job;

/* ---------------------------- */

/* ----- Global variables ----- */
char need_to_find[5] = " |\'\"";
job *job_list;
int max_job_idx;
job *fg_job; // job pointer of foreground

// For ignoring Ctrl+C when shell is on foreground
static sigjmp_buf env;
static volatile sig_atomic_t jump_active = 0;

// For handling the terminal
struct termios shell_tmodes; 
int shell_terminal_in;
int shell_interactive;
pid_t shell_pgid;

/* ---------------------------- */

/* ---- Function prototypes ---- */

void eval(char *cmdline);

int check_bg(char *string, int *last_idx); // check if background of foreground
char *find_last_quotes(char qt, char *start, int last_idx); // find corresponding quotes
int parseline(char *buf, char **argv, int *pipe_idx, int *pipe_num); // parse command line to words

int builtin_command(char **argv); 

void free_memory(); // Free every allocated memories

void init_shell(); // Init global variables and myshell gains terminal

void init_sig(char flag); // Initializes Signals of process

char *strchr_2(char *string); // user-defined strchr() of finding multi-characters

void int_handler(int sig); 
void chld_handler(int sig); // user-defined shell-handlers

void add_proc_node(process **plist, process **temp, pid_t pid); // add process node to list
    
char is_job_done(job *jptr); // Indicates finish of job
job *add_job(process *pid_list, char *cmdline, char state, pid_t pgid);   
job* del_job(job *jptr); // job list management functions

void launch_job(process *plist, int flag, pid_t pgid, char *cmdline);
void put_job_fg(job *jptr, int cont);
void put_job_bg(job *jptr, int cont);
void wait_fg_job(job *jptr); // Manages fore/back ground jobs

void shell_to_fg(job *jptr); // Returns authority of terminal to shell

job *find_proc_location(pid_t pid, process **ploc); // Finds job address of given pid

char *assemble_blocks(char *cmdline); // Assemble word blocks to full command line

void wake_child(int sig); // Wake paused child

int read_int(char *line); // Invert number string to int

void update_max_idx(void); // Update max index number of job list

job *idx_to_ptr(int idx); // Returns job address of given index

/*      Functions provided by csapp       */
ssize_t sio_puts(char s[]);
ssize_t Sio_puts(char s[]);
void sio_error(char s[]);
void Sio_error(char s[]);
void unix_error(char *msg);
handler_t *Signal(int signum, handler_t *handler);
void Kill(pid_t, int signum);
void *Malloc(size_t size);
int Sigsuspend(const sigset_t *set);
void Sigaddset(sigset_t *set, int signum);
void Sigemptyset(sigset_t *set);
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
pid_t Fork(void);
char *Fgets(char *ptr, int n, FILE *stream);
static size_t sio_strlen(char s[]);
void app_error(char *msg);
