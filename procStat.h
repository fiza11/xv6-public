struct procStat {
 int pid; // PID of the process
 int runtime; // use suitable unit of time
 int num_run; // number of time the process is executed
 int current_queue; // current assigned queue
 int ticks[5]; // number of ticks each process recieved
};
