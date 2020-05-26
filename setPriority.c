#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc,char** argv)
{
	int pid, pr;
	if (argc < 3) {
		printf(1, "Invalid number of arguments\n");
		exit();
	}
	pid = atoi(argv[1]);
	priority = atoi(argv[2]);
	if (priority > 100 || priority < 0) {
		printf(1, "Enter valid priority\n");
		exit();
	}
	chpr(pid, priority);
	exit();
}
