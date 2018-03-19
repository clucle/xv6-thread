
/**
 * this file is simple user-level unix shell
 *
 * @author Dujin Jeong
 * @since  2018-03-16
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "trim.h"

const int MAX_INPUT_SIZE = 1024;

void InteractiveMode();
void BatchMode(char *path);


void ExecuteCommandLine(char *raw_cmd);
void ExecuteCommand(char *cmd);

// Exception Function
void ExecuteChangeDir(char *cmd);

int
main(int argc, char *argv[]) {
	
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

void
InteractiveMode(void) {
	
	char raw_input_string[MAX_INPUT_SIZE];

	while (1) {
		printf("prompt> ");
		fgets(raw_input_string, MAX_INPUT_SIZE, stdin);
		
		// delete new line at end of string
		size_t l_string = strlen(raw_input_string) - 1;
		if (*raw_input_string && raw_input_string[l_string] == '\n') {
			raw_input_string[l_string] = '\0';
		}

		ExecuteCommandLine(raw_input_string);
	}
}

void
BatchMode(char *path) {
	printf("%s\n", path);	
}

void
ExecuteCommandLine(char *raw_cmd) {
	char *token;
	char *cmd;

	token = strtok(raw_cmd, ";");

	pid_t pid, wpid;
	int status = 0;

	int isFork;

	do {
		cmd = trim(token);

		isFork = 1;

		if (strlen(cmd) > 1 && cmd[0] == 'c' && cmd[1] == 'd') {
			if (strlen(cmd) == 2 || cmd[2] == ' ') {
				ExecuteChangeDir(cmd);
				isFork = 0;
			}
		}

		if (isFork) {
			pid = fork();
			
			if (pid < 0) {
				printf("ERR : Fail Fork\n");
			} else if (pid == 0) {
				ExecuteCommand(cmd);
			}
		}

	} while ((token = strtok(NULL, ";")) != NULL);

	// wait all child process works
	while ((wpid = wait(&status)) > 0);
}

void
ExecuteCommand(char* cmd) {
	char **arguments = (char **)malloc(sizeof(char *) * 1);
	arguments[0] = "";
	execvp(cmd, arguments);
	printf("%s\n", strerror(errno));
}

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
