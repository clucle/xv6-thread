
/**
 * this file is simple user-level unix shell
 *
 * @author Dujin Jeong
 * @since  2018-03-16
 */

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>

#include "trim.h"

// Global Constants
const int MAX_INPUT_SIZE = 1024;
const int MAX_CMD_CNT = 20;

// Two Modes
void InteractiveMode();
void BatchMode(char *path);

// Execute General Command
void ExecuteCommandLine(char *raw_cmd);
void ExecuteCommand(char *cmd);

// Exception Function
void ExecuteChangeDir(char *cmd);
void SigHandler(int sig);

int
main(int argc, char *argv[]) {

	struct sigaction sa;
	sa.sa_handler = SigHandler;
	sigaction(SIGINT, &sa, NULL);

	if (argc < 2) {
		InteractiveMode();
	} else if (argc == 2) {
		BatchMode(argv[1]);
	} else {
		printf("ERR : Too many argument\n");
		printf("shell mode : usage: ./shell\n");
		printf("batch mode : usage: ./shell [batchfile]\n");
	}

	return 0;
}

/**
  * this function is used to ignore signal
  * @param[in] sig - signal number
  * @return void
  */
void
SigHandler(int sig) {
	if (sig == SIGINT) {
		printf("\n");
	}
}

/**
  * this function is used to execute Interactive Mode
  * @return void
  */
void
InteractiveMode(void) {
	
	char raw_input_string[MAX_INPUT_SIZE];

	while (1) {
		printf("prompt> ");
		
		strcpy(raw_input_string, "");
		fgets(raw_input_string, MAX_INPUT_SIZE, stdin);
				
		if (feof(stdin)) {
			printf("\n");
			exit(0);	
		}
		// delete new line at end of string
		size_t l_string = strlen(raw_input_string) - 1;
		
		if (*raw_input_string && raw_input_string[l_string] == '\n') {
			raw_input_string[l_string] = '\0';
		}
		ExecuteCommandLine(raw_input_string);
	}
}

/**
  * this function is used to execute Batch Mode
  * @param[in] path - file path to load commands
  * @return void
  */
void
BatchMode(char *path) {
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	fp = fopen(path, "r");

	if (fp == NULL) {
		exit(EXIT_FAILURE);
	}

	while ((read = getline(&line, &len, fp)) != -1) {
		if (feof(stdin)) {
			printf("\n");
			exit(0);
		}
		printf("%s", line);
		ExecuteCommandLine(line);
	}

	fclose(fp);
	
	if (line) {
		free(line);
	}
	exit(EXIT_SUCCESS);
}

/**
  * this function is used to parse by ';' character multi commands
  * general command is executed by ExecuteCommand
  * exception is executed by exception handle
  * @param[in] raw_cmd - multi commands or one command
  * @return void
  */
void
ExecuteCommandLine(char *raw_cmd) {
	char *token;
	char *cmd;

	token = strtok(raw_cmd, ";");

	pid_t pid;

	int isFork;

	pid_t child_pids[20];
	int cnt_childs = 0;

	do {
		if (token == NULL) continue;
		cmd = trim(token);

		isFork = 1;

		if (strlen(cmd) > 1 && cmd[0] == 'c' && cmd[1] == 'd') {
			if (strlen(cmd) == 2 || cmd[2] == ' ') {
				ExecuteChangeDir(cmd);
				isFork = 0;
			}
		}
		
		if (strlen(cmd) > 3 && cmd[0] == 'q' && cmd[1] == 'u' &&
							   cmd[2] == 'i' && cmd[3] == 't') {
			if (strlen(cmd) == 4 || cmd[4] == ' ') {
				exit(0);
			}
		}

		if (isFork) {
			pid = fork();
			
			if (pid < 0) {
				printf("ERR : Fail Fork\n");
			} else if (pid == 0) {
				ExecuteCommand(cmd);
			} else {
				child_pids[cnt_childs] = pid;
				cnt_childs++;
			}
		}

	} while ((token = strtok(NULL, ";")) != NULL);
	
	// wait all child process works
	if (pid > 0) {
		int i;
		for (i = 0; i < cnt_childs; i++) {
			waitpid(child_pids[i], NULL, 0);
		}
	}
}

/**
  * this function is used to execute one command line
  * @param[in] cmd - one command
  * @return void
  */
void
ExecuteCommand(char* cmd) {
	char *arguments[MAX_CMD_CNT];
	char *token;

	token = strtok(cmd, " ");

	int cnt_arg = 0;
	do {
		char* tmp_ptr = (char*)malloc(sizeof(char) * strlen(token));
		strcpy(tmp_ptr, token);
		arguments[cnt_arg] = tmp_ptr;
		cnt_arg++;
	} while ((token = strtok(NULL, " ")) != NULL);

	execvp(cmd, arguments);
	printf("%s\n", strerror(errno));
	exit(0);
}

/**
  * this function is used to handle exception command "cd"
  * @param[in] cmd - cd command
  * @return void
  */
void
ExecuteChangeDir(char* cmd) {
	if (strlen(cmd) == 2) {
		if (chdir(getenv("HOME")) < 0) {
			printf("%s\n", strerror(errno));
		}
		return ;
	}

	if (strlen(cmd) == 4 && cmd[3] == '~') {
		if (chdir(getenv("HOME")) < 0) {
			printf("%s\n", strerror(errno));
		}
		return ;
	}

	if (chdir(cmd + 3) < 0) {
		printf("%s\n", strerror(errno));
	}
}
