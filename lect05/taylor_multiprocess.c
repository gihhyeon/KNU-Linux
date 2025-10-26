#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>

#define N 4
#define MAXLINE 100

double calculate_sin_taylor(double x, int terms)
{
	double value = x;
    	double numer = x * x * x;
    	double denom = 6.; // 3!
    	int sign = -1;

    	for (int j = 1; j <= terms; j++) {
        	value += (double)sign * numer / denom;
        	numer *= x * x;
        	denom *= (2.*(double)j + 2.) * (2.*(double)j + 3.);
        	sign *= -1;
    	}	
    	return value;
}

void sinx_taylor(int num_elements, int terms, double* x, double* result)
{
	int pid;
	int status;
	int fd[2 * num_elements];
    	char message[MAXLINE], line[MAXLINE];
    	int length;

	for (int i = 0; i < num_elements; i++) {
		if (pipe(fd + 2*i) == -1) {
			perror("pipe");
		   	exit(1);
        	}

		pid = fork();

		if (pid == -1) {
          	 	perror("fork");
           		exit(1);
       		}

		if (pid == 0) {
			close(fd[2*i]);

			double value = calculate_sin_taylor(x[i], terms);

			sprintf(message, "%lf", value);
            		length = strlen(message) + 1;
            		write(fd[2*i + 1], message, length);

			close(fd[2*i + 1]);
			exit(i);
		} else {
			close(fd[2*i + 1]);
		}
	}

	for (int i = 0; i < num_elements; i++) {
		wait(&status);

		int child_id = status >> 8;

		read(fd[2*child_id], line, MAXLINE);

		result[child_id] = atof(line);

		close(fd[2*child_id]);
	}
}

int main()
{
	double x[N] = {0, M_PI/6., M_PI/3., 0.134};
	double res[N];

	sinx_taylor(N, 3, x, res);

	for(int i = 0; i < N; i++) {
		printf("sin(%.2f) by Taylor series = %f\n", x[i], res[i]);
		printf("sin(%.2f) = %f\n", x[i], sin(x[i]));
	}
	return 0;
}


