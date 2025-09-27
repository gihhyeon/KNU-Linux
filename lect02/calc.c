#include <stdio.h>

int main(int argc, char *argv[])
{
	int a, b, r;
	char op = argv[2][0];

	sscanf(argv[1], "%d", &a);
    	sscanf(argv[3], "%d", &b);

	if (op == '+') r = a + b;
	else if (op == '-') r = a - b;
	else if (op == 'x') r = a * b;
	else r = a / b;

	printf("%d\n", r);
	return 0;
}

	
