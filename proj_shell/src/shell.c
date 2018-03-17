
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

#include "trim.h"

const int MAX_INPUT_SIZE = 1024;

void InteractiveMode();
void BatchMode(char *path);


void RunCommand(char *raw_cmd);

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
		if (*raw_input_string && raw_input_string[l_string] == '\n')
			raw_input_string[l_string] = '\0';

		RunCommand(raw_input_string);
	}
}

void
BatchMode(char *path) {
	printf("%s\n", path);	
}

void
RunCommand(char *raw_cmd) {
	char *token;
	char *cmd;

	token = strtok(raw_cmd, ";");

	do {
		cmd = trim(token);
		printf("token : %s %s\n", token, cmd);

	} while ((token = strtok(NULL, ";")) != NULL);

	char **arguments = (char **)malloc(sizeof(char *) * 1);
	arguments[0] = "";
	execvp(cmd, arguments);
}
