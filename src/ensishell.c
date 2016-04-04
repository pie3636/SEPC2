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
		

		
		if (l->err) {
			/* Syntax error, read another command */
			printf("error: %s\n", l->err);
			continue;
		}

		if (l->in) printf("in: %s\n", l->in);
		if (l->out) printf("out: %s\n", l->out);
		/: if (l->bg) printf("background (&)\n") /* struct list of background threads - add this thread */ ;

		/* Display each command of the pipe */
		/*
		for (i=0; l->seq[i]!=0; i++) {
			char **cmd = l->seq[i];
			printf("seq[%d]: ", i);
                        for (j=0; cmd[j]!=0; j++) {
                                printf("'%s' ", cmd[j]);
                        }
			printf("\n");
		}
		*/
		
		int status;
		
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
			/*toto = */waitpid(pid, &status, l->bg ? WNOHANG : 0);
			//if toto != 0 -> update status
		}
		
		/*if command = jobs
			int toto;
			do {	// for elements in our stated list only?!
				toto = waitpid(-1, &status, WNOHANG);
				if toto != 0
					update status
				// if (WIFEXITED(toto) == true) printf("[Done]  ", pid); // What if the exit is an error?
				// else printf("[Running]  ", pid);
			} while (toto != 0);*/
	}	

}
