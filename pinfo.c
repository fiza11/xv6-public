#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "mmu.h"
#include "param.h"
#include "proc.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    struct proc_stat pr;
    if(argc == 2){
        pr.pid = -1;
        pr.num_run = -1;
        pr.runtime = -1;
        if(getpinfo(&pr, atoi(argv[1])) == 0){
            printf(1, "No such process\n");
        }
        else printf(1, "pid = %d\nruntime = %d\nnum_run = %d\n", pr.pid, pr.runtime, pr.num_run);
    }
    else{
        printf(2, "Incorrect number of arguments. Correct way: pinfo <pid>\n");
    }
    exit();
}
