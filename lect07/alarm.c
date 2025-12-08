#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

void alarmHandler(int signo)
{
	printf("Wake up\n");
	exit(0);
}

int main()
{
	signal(SIGALRM, alarmHandler);
	alarm(5);
	printf("loop... \n");
	while (1) {
		sleep(1);
		printf("1 sec... \n");
	}
	printf("End of main \n");
}


