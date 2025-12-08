#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

void intHandler();

int main()
{
	signal(SIGINT, intHandler);
	sleep(3);
	raise(SIGINT);
	printf("End of main \n");
}

void intHandler(int signo)
{
	printf("SIGINT \n");
	printf("Sig No.: %d\n", signo);
	exit(0);
}
