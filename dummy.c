#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[]){
	int n, id, i;
	if (argc < 2)
		n = 1;
	else
		n = atoi(argv[1]);
	if (n < 0 || n > 20)
		n = 2;
    volatile int x = 0;
	for (int k = 0; k < n; k++) {
		id = fork();
		if (id < 0)
			printf(1, "%d failed in fork! \n", getpid());
		else if (id > 0) {
			printf(1, "Parent %d creating child %d\n", getpid(), id);
			wait();
		}
		else{
			printf(1, "Child %d created\n", getpid());
			for (i = 0; i <= 1000000000; i++) {
				// useless calculation to consume cpu time
				x = x - 1;
				x = x * 5;
				x = x + 1;
				x = x/1;
		    }
		    break;
		}
	}
	exit();
}
