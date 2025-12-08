#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main()
{
	printf("Parent process \n");
	if (fork() == 0) {
		printf("Child process \n");
		execl("/bin/echo", "echo", "hello", NULL);
		printf("End of child process \n");
	}
	printf("End of parent process\n");
	return 0;
}
