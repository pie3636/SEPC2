/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>

#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

#if USE_GUILE == 1
#include <libguile.h>

typedef struct pid_list {
    int pid;
    char* command;
    int isFinished; // 0 if running, 1 if finished, 2 is finished and printed (to be deleted)
    struct timeval start; // Time at which the process was started
    struct pid_list* next;
} pid_list;

pid_list* jobs = NULL;

int readAndRun(struct cmdline* l, int* status); // Reads and executes a command
void sig_handler(int signo); // Handles the SIGCHLD signals
int executer(char *line);
SCM executer_wrapper(SCM x);
void terminate(char *line);

void sig_handler(int signo) {
	if (signo == SIGCHLD) {
		struct timeval stop;
		pid_t pid;
		int status;
		long int timeDifference;
		while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { // Look for any terminated processes
			gettimeofday(&stop, NULL);
			int count = 1;
		    pid_list* current = jobs, *cur;
		    while (current != NULL && current->pid != pid) { // Find them in the list
		    	current = current->next;
		    	count++;
	    	}
	    	if (current != NULL) {
				timeDifference = stop.tv_sec * 1000000 + stop.tv_usec - current->start.tv_usec - current->start.tv_sec * 1000000; // Time difference in microseconds
		    	printf("\n[%d]\t%d\t%s\tTerminé - durée %ld.%06ld s\nensishell>", count, pid, current->command, timeDifference / 1000000, timeDifference % 1000000);
		    	fflush(stdout);
		        if (jobs == current) { // Remove the from the list (head)
		            cur = jobs->next;
		            free(jobs);
		            jobs = cur;
		        } else { // Continued (tail)
		            cur = jobs;
		            while (cur->next != current) {
		                cur = cur->next;
		            }
		            cur->next = cur->next->next;
		            free(current);
		        }
	        }
		}
	}
}

int executer(char *line) {
	int status;
    return readAndRun(parsecmd(&line), &status);
}

SCM executer_wrapper(SCM x) {
    return scm_from_int(executer(scm_to_locale_stringn(x, 0)));
}
#endif


void terminate(char *line) {
#ifdef USE_GNU_READLINE
    /* rl_clear_history() does not exist yet in centOS 6 */
    clear_history();
#endif
    if (line) {
        free(line);
    }
    printf("exit\n");
    exit(0);
}


int main() {
    printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);
    
    signal(SIGCHLD, sig_handler); // Catches all SIGCHLD signals

#ifdef USE_GUILE
    scm_init_guile();
    /* register "executer" function in scheme */
    scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

    int returnStatus, status;

    while (1) {
        struct cmdline *l;
        char *line = 0;
        //int i, j;
        char *prompt = "ensishell>";

        /* Readline use some internal memory structure that
           can not be cleaned at the end of the program. Thus
           one memory leak per command seems unavoidable yet */
        line = readline(prompt);
        if (line == 0 || ! strncmp(line,"exit", 4)) {
            terminate(line);
        }

#ifdef USE_GNU_READLINE
        add_history(line);
#endif


#ifdef USE_GUILE
        /* The line is a scheme command */
        if (line[0] == '(') {
            char catchligne[strlen(line) + 256];
            sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
            scm_eval_string(scm_from_locale_string(catchligne));
            free(line);
            continue;
        }
#endif

        /* parsecmd free line and set it up to 0 */
        l = parsecmd(& line);

        /* If input stream closed, normal termination */
        if (!l) {
            terminate(0);
        }
        
        returnStatus = readAndRun(l, &status);
        if (returnStatus) {
        	exit(returnStatus);
        }
        if (status) { // For direct calls to "exit"
        	exit(0);
        }
    }	
}

int readAndRun(struct cmdline* l, int* returnStatus) {
	int status, pfd[2], pid; // pfd contains the pipe connections.
	*returnStatus = 0;

	if (l->seq != NULL && l->seq[0] != NULL && !strcmp(l->seq[0][0], "exit")) { // First command = exit
		*returnStatus = 1;
	    return 0;
	}
	if (l->seq != NULL && l->seq[0] != NULL && l->seq[1] != NULL) {
		if (!strcmp(l->seq[0][0], "exit")) {
			*returnStatus = 1;
		    return 0;
		}
		if (pipe(pfd) == -1 || (pid = fork()) < 0) {
		    perror("Pipe or fork failed\n");
		    return 1;
		}
		if (pid == 0) { // Child
		    if(l->in != NULL) {         
		        int inputfile=open(l->in, O_RDONLY); // Opens the input file
		        if(inputfile == -1) {
		            printf("Error while opening input file\n");
		            return 0;
		        }
		        dup2(inputfile, 0);
		        close(inputfile);
		    } 
		    dup2(pfd[1], 1);
		    close(pfd[1]);
		    close(pfd[0]);
		    execvp(l->seq[0][0], l->seq[0]); // Executes the child and leaves
		    exit(0);
		} else {
			fflush(stdout);
		}
		if (!strcmp(l->seq[1][0], "exit")) {
		    *returnStatus = 1;
		    return 0;
		}
		pid = fork();
		if (pid == 0) {
		    dup2(pfd[0],0);
		    close(pfd[1]);
		    close(pfd[0]);
		    if(l->out != NULL) { // Opens the output file
		        int outputfile = open(l->out, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
		        if (outputfile == -1) {
		            printf("Error while opening output file\n");
		            exit(0);
		        }
		        dup2(outputfile, 1);
		        close(outputfile);
		    }
		    execvp(l->seq[1][0], l->seq[1]);
		    exit(0);
		}
		close(pfd[0]);
		close(pfd[1]);
		if (!l->bg) {
			waitpid(pid, &status, 0); // Wait for the children to be finished
		}
	} else { // Syntax error, read another command
		if (l->err) {
		    printf("Error: %s\n", l->err);
		    return 0;
		}
		if (l->seq != NULL && l->seq[0] != NULL && !strncmp(l->seq[0][0], "jobs", 4)) {
		    if (jobs == NULL) {
		        printf("No jobs found\n");
		    } else {
		        int count = 1, jobs_count = 0;
		        pid_list* current = jobs, *cur;
		        while (current != NULL) {
		            if (current->isFinished) { // Removes current from the list
		                if (jobs == current) { // Head of list
		                    cur = jobs->next;
		                    free(jobs);
		                    jobs = cur;
		                } else { // Tail
		                    cur = jobs;
		                    while (cur->next != current) {
		                        cur = cur->next;
		                    }
		                    cur->next = cur->next->next;
		                    free(current);
		                }
		            } else { // Process isn't done, or hasn't been viewed
		                current->isFinished = waitpid(current->pid, &status, WNOHANG);
		                jobs_count++;
		                printf("[%d]\t%s\t%d\t%s\n", count, current->isFinished ? "Terminé\t\0" : "En cours\0", current->pid, current->command);
		            }
		            current = current->next;
		            count++;
		        }
		        if (jobs_count == 0) {
		            printf("No jobs found\n");
		        }
		    }
		} else if (l->seq != NULL && l->seq[0] != NULL && !strncmp(l->seq[0][0], "ulimit", 6)) {
			struct rlimit limits;
			limits.rlim_cur = atoi(l->seq[0][1]);
			limits.rlim_max = limits.rlim_cur + 5;
			setrlimit(RLIMIT_CPU, &limits);
		} else if (l->seq != NULL && l->seq[0] != NULL) { // Pipes
		    pid_t pid = fork();
		    struct timeval time;
		    if (pid < 0) {
		        printf("Couldn't fork!\n");
		        return 1;
		    } else if (pid == 0) { // Child
				if (l->in) {
					int fd0 = open(l->in, O_RDONLY);
					dup2(fd0, STDIN_FILENO);
					close(fd0);
				}
				if (l->out) {
					int fd1 = open(l->out, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
					dup2(fd1, STDOUT_FILENO);
					close(fd1);
				}
				execvp(l->seq[0][0], l->seq[0]);
				perror("Failed to execute command\n");
				exit(1);
		    } else { // Parent
		    	gettimeofday(&time, NULL); // Executed as soon as possible to be more accurate
		        if (l->bg) {
			        printf("Detaching after fork from child process %d.\n", pid);
		            pid_list* new_job = malloc(sizeof(pid_list)); // Setting a new job in the list
		            new_job->pid = pid;
		            new_job->command = malloc(sizeof(char) * strlen(l->seq[0][0])); // TODO: Copy entire command?
		            new_job->isFinished = 0;
		            new_job->next = jobs;
		            new_job->start = time;
		            strncpy(new_job->command, l->seq[0][0], strlen(l->seq[0][0]));
		            jobs = new_job;
		            if (waitpid(pid, &status, WNOHANG)) {
		                jobs = jobs->next;
		                free(new_job);
		            }
		        } else {
		            waitpid(pid, &status, 0); // Only actually wait if this isn't a background task
		        }
		    }
		}	
	}
	return 0;
}
