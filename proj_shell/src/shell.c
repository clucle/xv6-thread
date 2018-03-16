
/**
 * this file is simple user-level unix shell
 *
 * @author Dujin Jeong
 * @since  2018-03-16
 */

#include <stdio.h>
#include <string.h>

const int MAX_INPUT_SIZE = 1024;

void InteractiveMode();
void BatchMode(char *path);

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
		printf("%s", raw_input_string); 
	}
}

void
BatchMode(char *path) {
	printf("%s\n", path);	
}

