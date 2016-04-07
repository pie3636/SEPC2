/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>

#include <sys/wait.h>

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
	struct pid_list* next;
} pid_list;

int executer(char *line)
{
	struct cmdline* inputCommand = parsecmd(&line);
	pid_t pid = fork();
	if (pid == -1) {
		printf("Couldn't fork!\n");
		exit(0);
	} else if (pid == 0) { // Fils
		return execvp(inputCommand->seq[0][0], inputCommand->seq[0]);
	} else { // Père
	}
	
	return 0;
}

SCM executer_wrapper(SCM x)
{
        return scm_from_int(executer(scm_to_locale_stringn(x, 0)));
}
#endif


void terminate(char *line) {
#ifdef USE_GNU_READLINE
	/* rl_clear_history() does not exist yet in centOS 6 */
	clear_history();
#endif
	if (line)
	  free(line);
	printf("exit\n");
	exit(0);
}


int main() {
        printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#ifdef USE_GUILE
        scm_init_guile();
        /* register "executer" function in scheme */
        scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

		pid_list* jobs = NULL;

	while (1) {
		struct cmdline *l;
		char *line=0;
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
		l = parsecmd( & line);

		/* If input stream closed, normal termination */
		if (!l) {
			terminate(0);
		}
		
		// Start type
		if (l->seq[1] != NULL) {
			int pfd[2];
			if (pipe(pfd) == -1) {
				perror("Pipe failed\n");
			} else {
			 
				int pid;
			   	if ((pid = fork()) < 0) {
					printf("fork failed\n");
				} else {
			 		if (pid == 0) { // Child
						close(pfd[1]);
						dup2(pfd[0], 0); // Connect the read side with stdin
						close(pfd[0]);
						execlp(l->seq[1][0], *(l->seq[1]), NULL);
						perror("Command failed"); // Execlp shouldn't return
					} else { // Parent
						close(pfd[0]);
						dup2(pfd[1], 1); // Connect the write side with stdout
						close(pfd[1]);
						execlp(l->seq[0][0], *(l->seq[0]), NULL);
						perror("Command failed"); // Execlp shouldn't return
					}
				}
			}
		} else {
			if (l->err) {
				/* Syntax error, read another command */
				printf("error: %s\n", l->err);
				continue;
			}

			if (l->in) printf("in: %s\n", l->in);
			if (l->out) printf("out: %s\n", l->out);
		
			int status;
		
			if (l->seq[0] != NULL && !strncmp(l->seq[0][0], "jobs", 4)) {
				if (jobs == NULL) {
					printf("No jobs found\n");
				} else {
					int count = 1;
					pid_list* current = jobs, *cur;
					while (current != NULL) {
						if (current->isFinished) { // Suppression de la liste
							if (jobs == current) {
								cur = jobs->next;
								free(jobs);
								jobs = cur;
							} else {
								cur = jobs;
								while (cur->next != current) {
									cur = cur->next;
								}
								cur->next = cur->next->next;
								free(current);
							}
						} else {
							current->isFinished = waitpid(current->pid, &status, WNOHANG);
							printf("[%d]\t%s\t%d\t%s\n", count, current->isFinished ? "Terminé\t\0" : "En cours\0", current->pid, current->command);
						}
						current = current->next;
						count++;
					}
				}
			} else {
				pid_t pid = fork();
				if (pid == -1) {
					printf("Couldn't fork!\n");
					exit(0);
				} else if (pid == 0) { // Fils
					int ret = execvp(l->seq[0][0], l->seq[0]);
					if (ret != 0) {
						printf("An error occurred while executing command\n");
					}
					return ret;
				} else { // Père
					if (l->bg) {
						printf("Detaching after fork from child process %d.\n", pid);
						pid_list* new_job = malloc(sizeof(pid_list));
						new_job->pid = pid;
						new_job->command = malloc(sizeof(char) * strlen(l->seq[0][0])); // TODO: Copy entire command?
						new_job->isFinished = 0;
						new_job->next = jobs;
						strncpy(new_job->command, l->seq[0][0], strlen(l->seq[0][0]));
						jobs = new_job;
						if (waitpid(pid, &status, WNOHANG)) {
							jobs = jobs->next;
							free(new_job);
						}
					} else {
						waitpid(pid, &status, 0);
					}
				}
			}	
		}	
	}	
}
