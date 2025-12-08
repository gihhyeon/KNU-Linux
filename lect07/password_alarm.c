#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#define PASSWORD "123"

void timeout_handler(int signo)
{
	printf("Fire!\n");
	exit(1);
}

void reset_handler(int signo)
{
	printf("10 sec reset...\n");
	alarm(10);
}

int main()
{
	char buffer[100];

	alarm(10);
	
	signal(SIGALRM, timeout_handler);
	signal(SIGINT, reset_handler);

	while (1) {
		int result = scanf("%s", buffer);

		if (result == EOF) {
			clearerr(stdin);
            		continue;
		}

		if (strcmp(buffer, PASSWORD) == 0) {
			alarm(0);
			printf("Correct!\n");
			exit(0);
		}
	}

	return 0;
}
				
	
	
