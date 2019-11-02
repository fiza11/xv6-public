#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "fs.h"

int main(int argc, char *argv[])
{

	int pid;
	int status = 0, a, b;
	pid = fork();
	if (pid == 0)
	{
		long long int z, x = 0;
		printf(1, "I'm Child Process\n");
		for (z = 0; z < 2147483648; z += 1)
			x = x + x; // useless calculations to consume CPU time
		exit();
	}
	else
	{
		status = waitx(&a, &b);
		printf(1, "Wait Time = %d\nRun Time = %d with Status %d \n", a, b, status);
	}

	exit();
}
