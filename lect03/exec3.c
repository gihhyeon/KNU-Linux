#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main()
{
	if (fork() == 0) {
		char *argv[3];
		argv[0] = "echo";argv[1]="hello";argv[2]=NULL;
		execl("/bin/echo", "echo", "hello", NULL);
	}
	printf("End of parent process\n");
	return 0;
}
