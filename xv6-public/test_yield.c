#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
	int pid;

	pid = fork();
	int i = 0;

	while (i < 10) {

		yield();
		if (pid == 0) {
			printf(1, "Child\n");
		} else {
			printf(1, "Parent\n");
		}
		
		i++;
	}

	if (pid != 0) wait();

	exit();
}
