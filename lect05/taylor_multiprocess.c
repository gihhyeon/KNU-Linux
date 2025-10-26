#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>
#define N 4
void sinx_taylor(int num_elemnets, int terms, double* x, double* result)
{
	int pid;
	int child_id;
	int fd[2*n], length;
	char message[MAXLINE], line[MAXLINE];

	for (int i = 0; i < num_elements; i++) {
		pipe(fd + 2*i);

		child_id = i;
		pid = fork();

		if (pid == 0) {
			close(fd[2*i]);

			double value = x[i];
               		double numer = x[i] * x[i] * x[i];
                	double denom = 6.; // 3!
                	int sign = -1;


			for(int j=1; j<=terms; j++) {
				value += (double)sign * numer / denom;
				numer *= x[i] * x[i];
				denom *= (2.*(double)j+2.) * (2.*(double)j+3.);
				sign *= -1;
		}

		result[i] = value;

		sprintf(message, "%lf", result[i]);
		length = strlen(message) + 1;
		write(fd[2*i + 1], message, length);

		exit(child_id);
		} else {
			for (int i = 0; i < num_elements; i++) {
				int status;
				wait(&status);
				int child_id = status >> 8;:
				read(fd[2*child_id], line, MAXLINE);
				result[child_id] = atof(line);
		}
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


