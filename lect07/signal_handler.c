#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

void intHandler();

int main()
{
	signal(SIGINT, intHandler);
	//signal(SIGINT, SIG_IGN);
	while (1)
		pause();
	printf("End of main \n");
}

void intHandler(int signo)
{
	printf("SIGINT \n");
	printf("Sig No.: %d\n", signo);
	exit(0);
	//signal(SIGINT, SIG_DFL); // 시그널 2번 발생시 프로세스 종료하도록
}
