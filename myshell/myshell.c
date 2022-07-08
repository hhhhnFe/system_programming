#include "myshell.h"

/* ----- $begin shellmain ----- */
int main() 
{
    char cmdline[MAXLINE]; /* Command line */
    sigset_t mask;

    init_shell();

    init_sig(0);
    Signal(SIGINT, int_handler);
    Signal(SIGCHLD, chld_handler);
    Sigemptyset(&mask);
    Sigaddset(&mask, SIGCHLD);
    Sigprocmask(SIG_BLOCK, &mask, NULL);

    while (1) 
    {
        if(sigsetjmp(env, 1) == 42)
            Sio_puts("\n");
        jump_active = 1;
	    Sio_puts("> ");                   
	    Fgets(cmdline, MAXLINE, stdin); // Read command line
	    if (feof(stdin))
	        exit(0);
        Signal(SIGINT, SIG_IGN);
	    eval(cmdline);
        Signal(SIGINT, int_handler);
    } 
}
/* $end shellmain */
void init_shell()
{
    job_list = NULL;
    max_job_idx = 0;
    
    shell_terminal_in = STDIN_FILENO;
    shell_interactive = isatty(shell_terminal_in);
    if(shell_interactive)
    {
        while (tcgetpgrp(shell_terminal_in) != (shell_pgid = getpgrp()))
            kill (- shell_pgid, SIGTTIN);

        init_sig(0);
        Signal(SIGINT, int_handler);
        Signal(SIGCHLD, chld_handler);
        shell_pgid = getpid();
        if(setpgid(shell_pgid, shell_pgid) < 0)
        {
            Sio_error("Shell pgid error");
            exit(1);
        }
        tcsetpgrp(shell_terminal_in, shell_pgid);
        tcgetattr(shell_terminal_in, &shell_tmodes);
    }
}

/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) 
{
    char *argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXLINE];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */

    int pipefd[MAXARGS][2];
    int pipe_idx[MAXARGS] = {0};
    int pipe_num = 0;

    process *plist = NULL, *temp = NULL;
    pid_t pid, pgid;
    job* jptr;

    sigset_t mask, prev;

    strcpy(buf, cmdline);
    bg = parseline(buf, argv, pipe_idx, &pipe_num);

    if (argv[0] == NULL)  
	    return;   /* Ignore empty lines */
    
    if (!builtin_command(argv)) 
    { 
        Sigemptyset(&mask);
        Sigaddset(&mask, SIGCONT);
        Sigprocmask(SIG_BLOCK, &mask, &prev);
        for(int i=0; i < pipe_num; i++)
        {
            if(pipe(pipefd[i]) < 0)
            {
                Sio_error("Pipe Failed");
                return;
            }   
        }

        pgid = 0; 
        for(int i = 0; i <= pipe_num; i++)
        {
            if((pid = Fork()) == 0)
            {
                // Reset child's signal
                init_sig(1);
                Signal(SIGCONT, wake_child);
                
                // pause until setpgid() is finished
                Sigemptyset(&prev);
                Sigsuspend(&prev);
                /* ----- pipe ----- */
                if(i)
                {
                    close(STDIN_FILENO);
                    dup2(pipefd[i-1][0], STDIN_FILENO);
                    close(pipefd[i-1][0]);
                }
                if(i < pipe_num)
                {
                    close(STDOUT_FILENO);
                    dup2(pipefd[i][1], STDOUT_FILENO);
                    close(pipefd[i][1]);
                }
                else if(i) close(pipefd[i-1][1]);
                /* ----------------- */

                char path[MAXLINE] = "/bin/";
                if (execve(strcat(path, argv[pipe_idx[i]]), argv+pipe_idx[i], environ) < 0)
                {	//ex) /bin/ls ls -al &
                    char usr_path[MAXLINE] = "/usr/bin/";
                    if (execve(strcat(usr_path, argv[pipe_idx[i]]), argv+pipe_idx[i], environ) < 0)
                    {
                        Sio_error("Command not found.\n");
                        exit(0);
                    }
                }
            } // End of Child Process

            if(i < pipe_num) 
                close(pipefd[i][1]);
            
            if(!i) pgid = pid;
            add_proc_node(&plist, &temp, pid);

        } // Endfor

        
        for(temp = plist; temp != NULL; temp = temp -> next)
            setpgid(temp->pid, pgid);
        Kill(-pgid, SIGCONT); 
        Sigprocmask(SIG_SETMASK, &prev, NULL); // set job's pgid and wake paused children

        launch_job(plist, bg, pgid, cmdline); // Launch Job
    }
    return;
}
/* $end eval */

void init_sig(char flag)
{
    if (flag == 0) // Ignore signals
    {
        Signal(SIGTSTP, SIG_IGN);
        Signal(SIGTTIN, SIG_IGN);
        Signal(SIGTTOU, SIG_IGN);
        Signal(SIGQUIT, SIG_IGN);
    }
    else // Return to default
    {
        Signal(SIGINT, SIG_DFL);
        Signal(SIGQUIT, SIG_DFL);
        Signal(SIGCHLD, SIG_DFL);
        Signal(SIGTSTP, SIG_DFL);
        Signal(SIGTTIN, SIG_DFL);
        Signal(SIGTTOU, SIG_DFL);
    }
}

void add_proc_node(process **plist, process **temp, pid_t pid)
{
    process *node;

    node = (process*)Malloc(sizeof(process));
    node -> pid = pid;
    node -> next = NULL;
    node -> flag = RUN;

    if(*plist == NULL) // First proces
    {
        (*plist) = node;
        (*temp) = node;
    }
    else
    {
        (*temp) -> next = node;
        (*temp) = node;
    }
    return;
}

void launch_job(process *plist, int flag, pid_t pgid, char *cmdline)
{
    if(flag == FG)
    {
        tcsetpgrp(shell_terminal_in, pgid); // foreground gains terminal
        put_job_fg(add_job(plist, assemble_blocks(cmdline), FG, pgid), 0);
    }
    else
        put_job_bg(add_job(plist, assemble_blocks(cmdline), FG, pgid), 0);
}

/* $begin builtin_command */
/* If first arg is a builtin command, run it and return true */
/* Else, return 0 */
/* When operating jobs, its index must be given in %d form */
int builtin_command(char **argv) 
{
    job *temp;

    if (!strcmp(argv[0], "exit")) /* quit command */
    {
        for(temp = job_list; temp; temp = temp -> next)
            Kill(-(temp->pgid), SIGINT);
        free_memory();
	    exit(0);
    }
    if (!strcmp(argv[0], "&"))    /* Ignore singleton & */
	return 1;
    if(!strcmp(argv[0], "cd"))    /* Changes directory */
    {
        if(chdir(argv[1]) < 0)
            Sio_puts("Failed to change directory\n");
        return 1;
    }
    if(!strcmp(argv[0], "jobs"))  /* List the running and stopped jobs */
    {
        if(job_list == NULL)
        {
            Sio_puts("Empty job list\n");
            return 1;
        }
        temp = job_list;
       
        // Print job list
        while(temp)
        {
            if(temp -> flag == RUN)
                printf("[%d] running %s\n", temp -> j_idx, temp -> cmd_line);
            else
                printf("[%d] suspended %s\n", temp -> j_idx, temp -> cmd_line);
            temp = temp -> next;
        }
        return 1;
    }
    if(!strcmp(argv[0], "bg"))    /* Change a stopped background job to a runing background job */
    {
        int jid = read_int(argv[1]+1);
        temp = idx_to_ptr(jid);
        if(temp == NULL)
        {
            Sio_puts("No Such Job\n");
            return 1;
        }
        printf("[%d] running %s\n", temp -> j_idx, temp -> cmd_line);
        temp -> flag = RUN;
        put_job_bg(temp, 1);
        return 1;
    }
    if(!strcmp(argv[0], "fg"))   /* Change a background job to foreground */
    {
        int jid = read_int(argv[1]+1);
        temp = idx_to_ptr(jid);
        if(temp == NULL)
        {
            Sio_puts("No Such Job\n");
            return 1;
        }
        printf("[%d] running %s\n", temp -> j_idx, temp -> cmd_line);
        temp -> flag = RUN;
        tcsetpgrp(shell_terminal_in, temp -> pgid); // Job gains terminal
        put_job_fg(temp, 1);
        return 1;
    } 
    if(!strcmp(argv[0], "kill"))  /* Terminate a job */
    {
        int jid = read_int(argv[1]+1);
        temp = idx_to_ptr(jid);
        if(temp == NULL)
        {
            Sio_puts("No Such Job\n");
            return 1;
        }
        temp -> flag = END; 
        kill(-(temp -> pgid), SIGINT); // Sends SIGINT to the job

        temp = del_job(temp);
        update_max_idx();    // Modify job related values
        return 1;
    }
    

    return 0;                     /* Not a builtin command */
}
/* $end builtin_command */

void free_memory()
{
    job *temp;
    process *tp;

    while(job_list)
    {
        temp = job_list;
        free(job_list -> cmd_line); // Free character array

        // Free process list of the job
        while(job_list -> plist != NULL)
        {
            tp = job_list -> plist;
            job_list -> plist = job_list -> plist -> next;
            free(tp);
        }

        free(temp); // Free job node
        job_list = job_list -> next;
    }
    return;
}

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
/* Returns whether given command line is bg or fg */
int parseline(char *buf, char **argv, int *pipe_idx, int *pipe_num) 
{
    char *delim, *temp;         /* Points to first space delimiter */
    char qt;
    int argc;            /* Number of args */
    int bg, last_idx;          

    bg = check_bg(buf, &last_idx);
    while (*buf && (*buf == ' ')) /* Ignore leading spaces */
	    buf++;

    /* Build the argv list */
    argc = 0;
    while ((delim = strchr_2(buf)))
    {
        if(*delim == '\'' || *delim == '\"') // Met quotation mark
        {
            qt = *delim;
            *delim = '\0';
            if (buf == delim) // If quote starts by itself
                delim = find_last_quotes(qt, ++buf, last_idx); 
            else // Quote is next to a letter
            {
                temp = delim+1;
                delim = find_last_quotes(qt, delim+1, last_idx);
                buf = strcat(buf, temp);
                *(delim-1) = '\0';
            }
        }
        if(*delim == ' ') // Met space
        {
            argv[argc++] = buf; // Add to argv
            *delim = '\0';
        }
        else if(*delim == '|') // Met pipeline
        {
            *pipe_num += 1;
            if (buf != delim) // pipeline comes right after a command
            {
                argv[argc++] = buf;
            }                    // Add to argv and add NULL 
            argv[argc++] = NULL; // to indicate a command has ended
            pipe_idx[*pipe_num] = argc; // Save pipe index
            *delim = '\0';
        }
        buf = delim + 1;
        while (*buf && (*buf == ' ')) buf++;
    }
    argv[argc] = NULL;

    return bg;
}
/* $end parseline */

int check_bg(char *string, int *last_idx)
{
    int i, ret=0;
    for(i = 0; string[i] != '\n'; i++) // Loops until \n or &
    {
        if(string[i] == '&')
        {
            *last_idx = i;
            string[i] = ' ';
            ret = 1;
        }
    }
    string[i] = ' ';
    *last_idx = i;
    return ret;
}

/* Searches for ''', '"', '|', ' ' in input string */
/* Returns its address if there is                 */
/* Else returns NULL                               */
char *strchr_2(char *string)
{
    while(*string != '\0')
    {
        if(*string == need_to_find[0] || \
           *string == need_to_find[1] || \
           *string == need_to_find[2] || \
           *string == need_to_find[3])
            return string;
        string++;
    }
    return NULL;
}

/* Handles quotation marks within or at the end of a string */
/* Returns the address that needs to be viewd next          */
char *find_last_quotes(char qt, char *start, int last_idx)
{
    char *quotes[MAXARGS];
    int i=0, j, k;
   
    for(j = 0; j <= last_idx; j++) // Marks every quotes in a word
    {
        if(start[j] == qt)
        {
            quotes[i++] = start+j;
            *(start+j) = '\0'; // Seperate both sides of quotes
        }
        if(i % 2 && *(start+j) == ' ')
            break;
    }
    i--;

    for(k = 1; k <= i-1; k++) // Concatenate separated blocks
    {
        if(strlen(quotes[k]+1) == 0)
            continue;
        start = strcat(start, quotes[k]+1);
    }

    j=0;
    for(k = quotes[i] - start + 1; k <= last_idx; k++) // Paste letters next to last quote
    {
        if(*(start + k) == '|' || *(start + k) == ' ')
        {
            while(quotes[i]-i+j < start+k) // Fill remaining spaces to \0
            {   
                *(quotes[i]-i+j) = '\0';
                j++;
            }
            break;
        }
        *(quotes[i] -i +j) = *(start + k);
        j++;
    }
    return start+k;
}

void int_handler(int sig)
{
    int olderrno = errno;
    int status, flag;
    sigset_t mask_all, prev_all;
    pid_t pid;

    // Gets new line
    if(!jump_active) return;
    siglongjmp(env, 42);
    errno = olderrno;

    return;
}

/* Adds job of which its process id is pid to job list */ 
/* Returns address of the job                          */
job *add_job(process *pid_list, char *cmdline, char state, pid_t pgid)
{
    job *node, *temp;

    // Allocate and initialize job node
    node = (job*)Malloc(sizeof(job));
    node -> plist = pid_list;
    node -> j_idx = ++max_job_idx;
    node -> pgid = pgid;
    node -> cmd_line = cmdline;    
    node -> next = NULL;
    node -> state = state;
    node -> flag = RUN;

    if (job_list == NULL) // First element
    {
        job_list = node;
    }
    else
    {
        temp = job_list;
        while(temp -> next != NULL)
            temp = temp -> next;
        temp -> next = node;
    }
    return node;
}

/* Delete and free job of which its address is jptr */
/* Returns address of next job of jptr              */
job *del_job(job *jptr)
{
    job *temp = job_list, *ret;
    process *tp;

    free(jptr -> cmd_line); // Free character array

    // Job is first element of the list
    if(job_list == NULL) return NULL;

    // Finds jptr in job_list
    if(job_list == jptr)
    {
        job_list = jptr -> next;
    }
    else
    {
        while(temp -> next != jptr)
            temp = temp -> next;
        temp -> next = jptr -> next;
    }

    // Free process list of the job
    while(jptr -> plist != NULL)
    {
        tp = jptr -> plist;
        jptr -> plist = jptr -> plist -> next;
        free(tp);
    }

    ret = jptr -> next;
    free(jptr); // Free job node

    return ret;
}

void put_job_fg(job *jptr, int cont)
{
    job *temp = job_list;

    if(cont) // If jptr is currently stopped, send SIGCONT to its processes
    {
        tcsetattr(shell_terminal_in, TCSADRAIN, &jptr->tmodes);
        Kill(-(jptr->pgid), SIGCONT);
    }
    wait_fg_job(jptr);
    while(temp) // Update job list : delete ended jobs and 
    {           //                   send stopped FG job to BG
        if(temp -> state == FG)
        {
            if(temp -> flag == STP) // FG stopped, print it and send it to background
            {                       
                printf("\n[%d] Stopped %s\n", temp -> j_idx, temp -> cmd_line);
                temp -> state = BG;
                temp = temp -> next;
                continue;
            }
            else
            {
                if(temp -> flag == END)
                {
                    temp = del_job(temp); // FG job ended, delete from job_list
                    continue;
                }
                else
                    temp = temp -> next;
            }
        }
        else
        { 
            if(temp -> flag == END)       // BG job ended, delete from job_list
                temp = del_job(temp);
            else                          // Job status did not change
                temp = temp -> next;   
        }
    }

    update_max_idx();

    /* Shell regains access to terminal */
    tcsetpgrp(shell_terminal_in, shell_pgid);
    tcgetattr(shell_terminal_in, &jptr -> tmodes);
    tcsetattr(shell_terminal_in, TCSADRAIN, &shell_tmodes);
    /* -------------------------------- */
}

void put_job_bg(job *jptr, int cont)
{
    if(cont)  // If jptr is currently stopped, sent SIGCONT to its processes
        Kill(-(jptr -> pgid), SIGCONT);
    return;
}

/* Returns address of the job of which its index is idx */
job *idx_to_ptr(int idx)
{
    job *temp = job_list;
    while(temp != NULL)
    {
        if(temp -> j_idx == idx)
            break;
        temp = temp -> next;
    }
    return temp;
}

/* Gets unmodified command line input and */
/* Returns true command line              */
char *assemble_blocks(char *cmdline)
{
    int i, j, flag = 0;
    char *ret = (char*)Malloc(sizeof(char)*MAXLINE);

    j=0;
    for(i = 0; cmdline[i] != '\n'; i++)
    {
        if(!flag && cmdline[i] == ' ') // Avoid repeated spaces
            continue;
        else
        {
            ret[j++] = cmdline[i];
            if(cmdline[i] == ' ') flag = 0;
            else flag = 1;
        }
    }
    ret[j] = '\0';
    return ret;
}

void wait_fg_job(job* jptr)
{

    int status;
    job *temp;
    process *ploc;
    pid_t pid;
    
    sigset_t mask;
    Sigemptyset(&mask);

    fg_job = jptr;
    while(jptr -> flag == RUN) // Shell suspends until FG job is done
    {
        Sigsuspend(&mask);
    }
    return;
}

void chld_handler(int sig)
{
    int olderrno = errno;
    int status;
    
    job *temp;
    process *ploc;
    pid_t pid;

    // Loops while FG job is running and there is finished process
    while(fg_job -> flag == RUN && (pid = waitpid(-1, &status, WUNTRACED)) > 0)
    {
        if(WIFSTOPPED(status)) // job stopped
        {
            if((temp = find_proc_location(pid, &ploc)) && \
                temp -> flag == RUN)
            {
                Kill(0, SIGTSTP);
                temp -> flag = STP;
                ploc -> flag = STP;
            }
        }
        else if(WIFSIGNALED(status) && WTERMSIG(status) == SIGINT) // job terminated
        {
            if((temp = find_proc_location(pid, &ploc)) && temp -> flag != END)
            {
                Kill(0, SIGINT);
                temp -> flag = END;
                ploc -> flag = END;
                Sio_puts("\n");
            }
        }   
        else
        {
            if((temp = find_proc_location(pid, &ploc))) 
            {
                ploc -> flag = END;
                if(is_job_done(temp)) // job done
                {
                    temp -> flag = END;
                }
            }
        }
    }
    errno = olderrno;
}

/* Returns 1 if job done */
/* Else 0                */
char is_job_done(job *jptr)
{
    process *temp = jptr -> plist;
    while(temp != NULL)
    {
        if(temp -> flag != END)
            return 0;
        temp = temp -> next;
    }
    return 1;
}

/* Find job that has process pid                   */
/* Returns its address and address of such process */
/* Returns NULL if there is no such job            */
job *find_proc_location(pid_t pid, process **ploc)
{
    job *tj = job_list;
    process *tp;

    while(tj != NULL) // Finds job
    {
        tp = tj -> plist;
        while(tp) // Finds process
        {
            if(tp -> pid == pid)
            {
                *ploc = tp;
                return tj;
            }
            tp = tp -> next;
        }
        tj = tj -> next;
    }
    return tj;
}

void wake_child(int sig)
{
    return;
}

int read_int(char *line)
{
    int ret = 0;
    while(*line != '\0')
    {
        ret *= 10;
        ret += (*line-'0');
        line++;
    }
    return ret;
}

void update_max_idx(void)
{
    job *temp = job_list;

    max_job_idx = 0;
    while(temp)
    {
        max_job_idx = temp -> j_idx;
        temp = temp -> next;
    }
}

/* ----------- END USER CODE ------------ */

ssize_t Sio_puts(char s[])
{
    ssize_t n;
    if((n = sio_puts(s)) < 0)
        sio_error("Sio_puts error");
    return n;
}

void Sio_error(char s[])
{
    sio_error(s);
}

handler_t *Signal(int signum, handler_t *handler)
{
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;

    if(sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return(old_action.sa_handler);
}

void Kill(pid_t pid, int signum)
{
    int rc;

    if((rc = kill(pid, signum)) < 0)
        unix_error("Kill error");
}

void *Malloc(size_t size)
{
    void *p;
    if((p = malloc(size)) == NULL)
        unix_error("Malloc error");
    return p;
}

ssize_t sio_puts(char s[]) /* Put string */
{
    return write(STDOUT_FILENO, s, sio_strlen(s)); //line:csapp:siostrlen

}
void sio_error(char s[]) /* Put error message and exit */
{
    sio_puts(s);
    _exit(1);                                      //line:csapp:sioexit
}
void unix_error(char *msg) /* Unix-style error */
{
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    exit(0);
}
void Sigemptyset(sigset_t *set)
{
    if (sigemptyset(set) < 0)
	unix_error("Sigemptyset error");
    return;
}
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    if (sigprocmask(how, set, oldset) < 0)
	unix_error("Sigprocmask error");
    return;
}
static size_t sio_strlen(char s[])
{
    int i = 0;

    while (s[i] != '\0')
        ++i;
    return i;
}
int Sigsuspend(const sigset_t *set)
{
    int rc = sigsuspend(set); /* always returns -1 */
    if (errno != EINTR)
        unix_error("Sigsuspend error");
    return rc;
}
void Sigaddset(sigset_t *set, int signum)
{
    if (sigaddset(set, signum) < 0)
	unix_error("Sigaddset error");
    return;
}
pid_t Fork(void) 
{
    pid_t pid;

    if ((pid = fork()) < 0)
	unix_error("Fork error");
    return pid;
}
char *Fgets(char *ptr, int n, FILE *stream) 
{
    char *rptr;

    if (((rptr = fgets(ptr, n, stream)) == NULL) && ferror(stream))
	app_error("Fgets error");

    return rptr;
}
void app_error(char *msg) /* Application error */
{
    fprintf(stderr, "%s\n", msg);
    exit(0);
}
