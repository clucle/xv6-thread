
/**
 * this file is simple user-level unix shell
 *
 * @author Dujin Jeong
 * @since  2018-03-16
 */

#include <stdio.h>

int
main(int argc, char *argv[]) {
	
	if (argc < 2) {
		// shell mode
	} else if (argc == 2) {
		// batch mode
	} else {
		printf("ERR : Too many argument\n");
		printf("shell mode : usage: ./shell\n");
		printf("batch mode : usage: ./shell [batchfile]\n");
	}
	return 0;
}
