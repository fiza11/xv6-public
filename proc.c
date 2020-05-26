#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "procStat.h"

struct {
	struct spinlock lock;
	struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);
struct priorityqueue *returnQueue(void);
void addToQueue(struct priorityqueue *pq, struct proc *p);
void removeFromQueue(struct priorityqueue *pq, struct proc *p);
void downgradeQueue(struct priorityqueue *pq, struct proc *p);

// Global variables to maintain info
int queue0[500], queue1[500], queue2[500], queue3[500], queue4[500];
char *name0[500], *name1[500], *name2[500], *name3[500], *name4[500];
char *state0[500], *state1[500], *state2[500], *state3[500], *state4[500];


void
pinit(void)
{
	initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
	return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
	struct cpu*
mycpu(void)
{
	int apicid, i;

	if(readeflags()&FL_IF)
		panic("mycpu called with interrupts enabled\n");

	apicid = lapicid();
	// APIC IDs are not guaranteed to be contiguous. Maybe we should have
	// a reverse map, or reserve a register to store &cpus[i].
	for (i = 0; i < ncpu; ++i) {
		if (cpus[i].apicid == apicid)
			return &cpus[i];
	}
	panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
	struct cpu *c;
	struct proc *p;
	pushcli();
	c = mycpu();
	p = c->proc;
	popcli();
	return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
	static struct proc*
allocproc(void)
{
	struct proc *p;
	char *sp;

	acquire(&ptable.lock);

	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		if(p->state == UNUSED)
			goto found;

	release(&ptable.lock);
	return 0;

found:
	p->state = EMBRYO;
	p->pid = nextpid++;
	p->priority = 60; //default prior
	addToQueue(&q0, p);
	release(&ptable.lock);

	// Allocate ker nel stack.
	if((p->kstack = kalloc()) == 0){
		p->state = UNUSED;
		return 0;
	}
	sp = p->kstack + KSTACKSIZE;

	// Leave room for trap frame.
	sp -= sizeof *p->tf;
	p->tf = (struct trapframe*)sp;

	// Set up new context to start executing at forkret,
	// which returns to trapret.
	sp -= 4;
	*(uint*)sp = (uint)trapret;

	sp -= sizeof *p->context;
	p->context = (struct context*)sp;
	memset(p->context, 0, sizeof *p->context);
	p->context->eip = (uint)forkret;

	p->stime = ticks;         // start time
	p->etime = 0;             // end time
	p->rtime = 0;             // run time
	// p->iotime = 0;    i
	p->wtime=0;        // I/O time
	return p;
}

//PAGEBREAK: 32
// Set up first user process.
	void
userinit(void)
{
	struct proc *p;
	extern char _binary_initcode_start[], _binary_initcode_size[];

	TOTAL_TICKS = 0;

	q0.end = 0;
	q1.end = 0;
	q2.end = 0;
	q3.end = 0;
	q4.end = 0;

	q0.numOfProc = 0;
	q1.numOfProc = 0;
	q2.numOfProc = 0;
	q3.numOfProc = 0;
	q4.numOfProc = 0;

	q0.ticks = 1;
	q1.ticks = 2;
	q2.ticks = 4;
	q3.ticks = 8;
	q4.ticks = 16;

	q0.priority = 0;
	q1.priority = 1;
	q2.priority = 2;
	q3.priority = 3;
	q4.priority = 4;

	p = allocproc();
	initproc = p;
	if((p->pgdir = setupkvm()) == 0)
		panic("userinit: out of memory?");
	inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
	p->sz = PGSIZE;
	memset(p->tf, 0, sizeof(*p->tf));
	p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
	p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
	p->tf->es = p->tf->ds;
	p->tf->ss = p->tf->ds;
	p->tf->eflags = FL_IF;
	p->tf->esp = PGSIZE;
	p->tf->eip = 0;  // beginning of initcode.S

	safestrcpy(p->name, "initcode", sizeof(p->name));
	p->cwd = namei("/");

	// this assignment to p->state lets other cores
	// run this process. the acquire forces the above
	// writes to be visible, and the lock is also needed
	// because the assignment might not be atomic.
	acquire(&ptable.lock);

	p->state = RUNNABLE;

	release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
	int
growproc(int n)
{
	uint sz;
	struct proc *curproc = myproc();

	sz = curproc->sz;
	if(n > 0){
		if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
			return -1;
	} else if(n < 0){
		if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
			return -1;
	}
	curproc->sz = sz;
	switchuvm(curproc);
	return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
	int
fork(void)
{
	int i, pid;
	struct proc *np;
	struct proc *curproc = myproc();

	// Allocate process.
	if((np = allocproc()) == 0){
		return -1;
	}

	// Copy process state from proc.
	if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
		kfree(np->kstack);
		np->kstack = 0;
		np->state = UNUSED;
		return -1;
	}
	np->sz = curproc->sz;
	np->parent = curproc;
	*np->tf = *curproc->tf;

	// Clear %eax so that fork returns 0 in the child.
	np->tf->eax = 0;

	for(i = 0; i < NOFILE; i++)
		if(curproc->ofile[i])
			np->ofile[i] = filedup(curproc->ofile[i]);
	np->cwd = idup(curproc->cwd);

	safestrcpy(np->name, curproc->name, sizeof(curproc->name));

	pid = np->pid;

	acquire(&ptable.lock);

	np->state = RUNNABLE;

	release(&ptable.lock);

	return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
	void
exit(void)
{
	struct proc *curproc = myproc();
	struct proc *p;
	int fd;

	if(curproc == initproc)
		panic("init exiting");

	// Close all open files.
	for(fd = 0; fd < NOFILE; fd++){
		if(curproc->ofile[fd]){
			fileclose(curproc->ofile[fd]);
			curproc->ofile[fd] = 0;
		}
	}

	begin_op();
	iput(curproc->cwd);
	end_op();
	curproc->cwd = 0;

	acquire(&ptable.lock);

	// Parent might be sleeping in wait().
	wakeup1(curproc->parent);

	// Pass abandoned children to init.
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->parent == curproc){
			p->parent = initproc;
			if(p->state == ZOMBIE)
				wakeup1(initproc);
		}
	}

	// Jump into the scheduler, never to return.
	curproc->state = ZOMBIE;
	curproc->etime=ticks;
	sched();
	panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
	int
wait(void)
{
	struct proc *p;
	int havekids, pid;
	struct proc *curproc = myproc();

	acquire(&ptable.lock);
	for(;;){
		// Scan through table looking for exited children.
		havekids = 0;
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			if(p->parent != curproc)
				continue;
			havekids = 1;
			if(p->state == ZOMBIE){
				// Found one.
				pid = p->pid;
				kfree(p->kstack);
				p->kstack = 0;
				freevm(p->pgdir);
				p->pid = 0;
				p->parent = 0;
				p->name[0] = 0;
				p->killed = 0;
				p->state = UNUSED;
				release(&ptable.lock);
				return pid;
			}
		}

		// No point waiting if we don't have any children.
		if(!havekids || curproc->killed){
			release(&ptable.lock);
			return -1;
		}

		// Wait for children to exit.  (See wakeup1 call in proc_exit.)
		sleep(curproc, &ptable.lock);  //DOC: wait-sleep
	}
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
struct priorityqueue *returnQueue(void)
{
	if (q0.numOfProc)
	{
		for (int i = 0; i < q0.end; i++)
			if (q0.queue[i]->state == RUNNABLE)
				return &q0;
	}

	if (q1.numOfProc)
	{
		for (int i = 0; i < q1.end; i++)
			if (q1.queue[i]->state == RUNNABLE)
				return &q1;
	}

	if (q2.numOfProc)
	{
		for (int i = 0; i < q2.end; i++)
			if (q2.queue[i]->state == RUNNABLE)
				return &q2;
	}

	if (q3.numOfProc)
	{
		for (int i = 0; i < q3.end; i++)
			if (q3.queue[i]->state == RUNNABLE)
				return &q3;
	}
	if (q4.numOfProc)
	{
		for (int i = 0; i < q4.end; i++)
			if (q4.queue[i]->state == RUNNABLE)
				return &q4;
	}

	//cprintf("No runnable processes");
	return &q0;
}

//Adds process to end of specified priority queue
void addToQueue(struct priorityqueue *pq, struct proc *p)
{
	pq->queue[pq->end] = p;
	pq->end++;
	pq->numOfProc++;
	p->current_queue = pq->priority;
	p->Ticks = 0;
}

//Removes process from the start of specified priority queue
void removeFromQueue(struct priorityqueue *pq, struct proc *p)
{
	if (pq->queue[0] != p)
		panic("LOOK IN REMOVE FROM QUEUE FUNCTION");

	int firstFreeIndex = pq->end;

	for (int i = 0; i < firstFreeIndex; i++)
		pq->queue[i] = pq->queue[i + 1];
	pq->numOfProc--;
	pq->end--;
}

//Downgrades process to a lower priority queue
void downgradeQueue(struct priorityqueue *pq, struct proc *p)
{
	if (!(pq->priority))
	{
		removeFromQueue(&q0, p);
		addToQueue(&q1, p);
	}

	else if (pq->priority == 1)
	{
		removeFromQueue(&q1, p);
		addToQueue(&q2, p);
	}
	else if (pq->priority == 2)
	{
		removeFromQueue(&q2, p);
		addToQueue(&q3, p);
	}
	else if (pq->priority == 3)
	{
		removeFromQueue(&q3, p);
		addToQueue(&q4, p);
	}
}

//Updates queue, total_ticks and wait_time fields
//for all processes every time a tick occurs
void updatePstat(void)
{
	struct proc *p;

	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
	{
		if (p->state == RUNNABLE)
		{
			//index 499 keeps getting overwritten
			//since TOTAL_TICKS never goes past NTICKS (i.e 500)
			// probably won't matter
			p->queue[TOTAL_TICKS] = p->current_queue;
			p->total_ticks++;

			if (p->current_queue == 4)
				p->wait_time++;
		}
	}
}

//Returns highest priority RUNNABLE process
struct proc *returnProc(void)
{
	struct priorityqueue *pq = returnQueue();

	if (pq->numOfProc == 0)
		//cprintf("In returnProc - no runnable process");
		return 0;

	for (int i = 0; i < pq->end; i++)
	{
		if (pq->queue[i]->state == RUNNABLE)
		{
			struct proc *p = pq->queue[i];

			if (i != 0)
			{
				//Swap with first
				struct proc *temp = pq->queue[0]; //i can do a change over here
				pq->queue[i] = temp;
				pq->queue[0] = p;
				// pq->queue[0]=pq->queue[0]+pq->queue[i]-(pq->queue[i]=pq->)
			}
			if(pq->priority==4)
			{

		removeFromQueue(&q4, p);
		addToQueue(&q4, p);
			}
			return p;
		}
	}

	//cprintf("At end of returnProc");
	return 0;
}

// Moves process from Q2 to Q1
void boost(struct proc *p)
{
	// removeFromQueue(&q4, p);
	// addToQueue(&q0, p);
	// p->current_queue = 0;
	if (p->current_queue==1)
	{
		removeFromQueue(&q1, p);
		addToQueue(&q0, p);
		p->current_queue=0;
	}
	else if (p->current_queue==2)
	{
		removeFromQueue(&q2, p);
		addToQueue(&q1, p);
		p->current_queue=1;
	}
	else if (p->current_queue==3)
	{
		removeFromQueue(&q3, p);
		addToQueue(&q2, p);
		p->current_queue=2;
	}
	else if (p->current_queue==4)
	{
		removeFromQueue(&q4, p);
		addToQueue(&q3, p);
		p->current_queue=3;
	}

}

	void
scheduler(void)
{
	// struct proc *p=0;

	struct cpu *c = mycpu();
	c->proc = 0;
	// int flag=0;
	struct proc *p=0;

	for(;;){
		// Enable interrupts on this processor.
		sti();

		// Loop over process table looking for process to run.
		acquire(&ptable.lock);
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			// flag=0;
			p->wtime=ticks-p->stime-p->rtime;
			#ifdef DEFAULT
				if(p->state != RUNNABLE)
					continue;

			#else
			#ifdef FCFS
				struct proc *minp = 0,*p1=0;
				 // cprintf("FCFS\n");

				if(p->state != RUNNABLE)
					continue;
			minp=p;
			for(p1= ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
				if((p1->state==RUNNABLE)&&(minp->stime>p1->stime))
					minp=p1;
			}
				// if(p->pid > 1)
    //         	{
    //           		if (minp != 0){
    //             // here I find the process with the lowest creation time (the first one that was created)
    //             		if(p->stime < minp->stime )
    //               			minp = p;
    //           		}
    //           		else
    //              	 minp = p;
    //         	}
				if(minp != 0 && minp->state == RUNNABLE)
					p = minp;
			#else
			#ifdef PBS
				struct proc *p1=0, *highP=0;
			// cprintf("PRIORITY\n");
				if(p->state != RUNNABLE)
					continue;
				highP=p;
				if(p->pid>2)
				for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){

					if(((highP->priority>p1->priority)||((highP->priority==p1->priority)&&(highP->rtime > p1->rtime))) && (p1->state == RUNNABLE ))
						highP=p1;
				}
				if(highP!=0)
					p=highP;
			#else
			#ifdef MLFQ
			// cprintf("MLFQ");
			if (p != returnProc())
				continue;
			// flag=1;
			p->num_run++;
			struct priorityqueue *queue = returnQueue();

			//printQueues();

			int queuepriority = p->current_queue, count = 0;


			//Each iteration of this loop is a tick!
			//If p->state goes to something other than runnable by end of this
			//loop then that means proc did not complete its tick
			while (p->state == RUNNABLE && count < queue->ticks)
			{
				if(p->current_queue == 0)
				{
					queue0[TOTAL_TICKS] = p->pid;
					name0[TOTAL_TICKS] = p->name;

				}
				if(p->current_queue == 1)
				{
					queue1[TOTAL_TICKS] = p->pid;
					name1[TOTAL_TICKS] = p->name;

				}
				if(p->current_queue == 2)
				{
					queue2[TOTAL_TICKS] = p->pid;
					name2[TOTAL_TICKS] = p->name;

				}
					if(p->current_queue == 3)
				{
					queue3[TOTAL_TICKS] = p->pid;
					name3[TOTAL_TICKS] = p->name;

				}	if(p->current_queue == 4)
				{
					queue4[TOTAL_TICKS] = p->pid;
					name4[TOTAL_TICKS] = p->name;

				}

				// Switch to chosen process.  It is the process's job
				// to release ptable.lock and then reacquire it
				// before jumping back to us.

				c->proc = p;

				switchuvm(p);
				p->state = RUNNING;

				swtch(&(c->scheduler), p->context);
				switchkvm();

				// Process is done running for now.
				// It should have changed its p->state before coming back.
				c->proc = 0;

				//If state is not RUNNABLE here then means tick did not complete


				//Added to maintain info about state when process RETURNS
				//to scheduler
				if(p->current_queue == 0)
				{

					if(p->state == RUNNABLE) state0[TOTAL_TICKS] = "Runnable";
					if(p->state == SLEEPING) state0[TOTAL_TICKS] = "Sleeping";
					if(p->state == ZOMBIE) state0[TOTAL_TICKS] = "Zombie";
				}
				if(p->current_queue == 1)
				{

					if(p->state == RUNNABLE) state1[TOTAL_TICKS] = "Runnable";
					if(p->state == SLEEPING) state1[TOTAL_TICKS] = "Sleeping";
					if(p->state == ZOMBIE) state1[TOTAL_TICKS] = "Zombie";
				}
				if(p->current_queue == 2)
				{

					if(p->state == RUNNABLE) state2[TOTAL_TICKS] = "Runnable";
					if(p->state == SLEEPING) state2[TOTAL_TICKS] = "Sleeping";
					if(p->state == ZOMBIE) state2[TOTAL_TICKS] = "Zombie";
				}
				if(p->current_queue == 3)
				{

					if(p->state == RUNNABLE) state3[TOTAL_TICKS] = "Runnable";
					if(p->state == SLEEPING) state3[TOTAL_TICKS] = "Sleeping";
					if(p->state == ZOMBIE) state3[TOTAL_TICKS] = "Zombie";
				}
				if(p->current_queue == 4)
				{

					if(p->state == RUNNABLE) state4[TOTAL_TICKS] = "Runnable";
					if(p->state == SLEEPING) state4[TOTAL_TICKS] = "Sleeping";
					if(p->state == ZOMBIE) state4[TOTAL_TICKS] = "Zombie";
				}

				count++;
				p->Ticks++;

				//Update queue pstat var of each process
				updatePstat();

				//Keep track of number of ticks, where each
				//tick is valid only if a process is being run
				//If no processes are being run then code below is not executed.
				if (TOTAL_TICKS < 500)
					TOTAL_TICKS++;
			}

			//Maintaining pstat info
			p->ticks[queuepriority] += count;
			p->times[p->current_queue] = p->times[p->current_queue] + 1;


			//Handles case where process uses its time slice and is demoted
			if (p->state == RUNNABLE && (p->current_queue == 1 || p->current_queue == 0 ||p->current_queue == 2||p->current_queue == 3 ))
			{
				downgradeQueue(queue, p);
			}

			//Handles case where process in Q2 exceeds 50 ticks
			else if (p->state == RUNNABLE && p->current_queue == 4 && p->Ticks >= 500)
			{
				boost(p);

			}
			else if (p->state == RUNNABLE && p->current_queue == 3 && p->Ticks >= 1000)
			{
				boost(p);

			}else if (p->state == RUNNABLE && p->current_queue == 2 && p->Ticks >= 2000)
			{
				boost(p);

			}else if (p->state == RUNNABLE && p->current_queue == 1 && p->Ticks >= 4000)
			{
				boost(p);

			}
			//Handles case where process does NOT use its timeslice
			else if (p->state == SLEEPING)
			{
				removeFromQueue(queue, p);
				addToQueue(queue, p);
			}
			// cprintf()

			 // cprintf(" pid %d  current_queue:%d\n",p->pid,p->current_queue);
			#endif
			#endif
			#endif
			#endif
			#ifndef MLFQ
			if(p!=0 )
			{
			c->proc = p;

			switchuvm(p);
			p->state = RUNNING;
			switchuvm(p);
			swtch(&(c->scheduler), p->context);
			switchkvm();
			c->proc = 0;
			}
			#endif
}
release(&ptable.lock);

	}
}
// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
	void
sched(void)
{
	int intena;
	struct proc *p = myproc();

	if(!holding(&ptable.lock))
		panic("sched ptable.lock");
	if(mycpu()->ncli != 1)
		panic("sched locks");
	if(p->state == RUNNING)
		panic("sched running");
	if(readeflags()&FL_IF)
		panic("sched interruptible");
	intena = mycpu()->intena;
	swtch(&p->context, mycpu()->scheduler);
	mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
	void
yield(void)
{
	acquire(&ptable.lock);  //DOC: yieldlock
	myproc()->state = RUNNABLE;
	// myproc()->cticks/++;
	sched();
	release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
	void
forkret(void)
{
	static int first = 1;
	// Still holding ptable.lock from scheduler.
	release(&ptable.lock);

	if (first) {
		// Some initialization functions must be run in the context
		// of a regular process (e.g., they call sleep), and thus cannot
		// be run from main().
		first = 0;
		iinit(ROOTDEV);
		initlog(ROOTDEV);
	}

	// Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
	void
sleep(void *chan, struct spinlock *lk)
{
	struct proc *p = myproc();

	if(p == 0)
		panic("sleep");

	if(lk == 0)
		panic("sleep without lk");

	// Must acquire ptable.lock in order to
	// change p->state and then call sched.
	// Once we hold ptable.lock, we can be
	// guaranteed that we won't miss any wakeup
	// (wakeup runs with ptable.lock locked),
	// so it's okay to release lk.
	if(lk != &ptable.lock){  //DOC: sleeplock0
		acquire(&ptable.lock);  //DOC: sleeplock1
		release(lk);
	}
	// Go to sleep.
	p->chan = chan;
	p->state = SLEEPING;

	sched();

	// Tidy up.
	p->chan = 0;

	// Reacquire original lock.
	if(lk != &ptable.lock){  //DOC: sleeplock2
		release(&ptable.lock);
		acquire(lk);
	}
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
	static void
wakeup1(void *chan)
{
	struct proc *p;

	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		if(p->state == SLEEPING && p->chan == chan)
			p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
	void
wakeup(void *chan)
{
	acquire(&ptable.lock);
	wakeup1(chan);
	release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
	int
kill(int pid)
{
	struct proc *p;

	acquire(&ptable.lock);
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->pid == pid){
			p->killed = 1;
			// Wake process from sleep if necessary.
			if(p->state == SLEEPING)
				p->state = RUNNABLE;
			release(&ptable.lock);
			return 0;
		}
	}
	release(&ptable.lock);
	return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
	void
procdump(void)
{
	static char *states[] = {
		[UNUSED]    "unused",
		[EMBRYO]    "embryo",
		[SLEEPING]  "sleep ",
		[RUNNABLE]  "runble",
		[RUNNING]   "run   ",
		[ZOMBIE]    "zombie"
	};
	int i;
	struct proc *p;
	char *state;
	uint pc[10];

	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->state == UNUSED)
			continue;
		if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
			state = states[p->state];
		else
			state = "???";
		cprintf("%d %s %s", p->pid, state, p->name);
		if(p->state == SLEEPING){
			getcallerpcs((uint*)p->context->ebp+2, pc);
			for(i=0; i<10 && pc[i] != 0; i++)
				cprintf(" %p", pc[i]);
		}
		cprintf("\n");
	}
}


int ps(void)
{
	struct proc* p;

	sti();

	acquire(&ptable.lock);
	cprintf("name\tpid\tstate\tpriority\tctime\trtime\twtime\n");
	cprintf("\n");
	for(p = ptable.proc; p <&ptable.proc[NPROC]; p++)
	{
		if(p->state == SLEEPING)
			cprintf("%s\t%d\tSLEEPING\t%d\t%d\t%d\t%d\n", p->name, p->pid, p->priority, p->stime, p->rtime, p->wtime);
		else if(p->state==RUNNING)
			cprintf("%s\t%d\tRUNNING\t%d\t%d\t%d\t%d\n", p->name, p->pid, p->priority, p->stime, p->rtime, p->wtime);
		else if(p->state==RUNNABLE)
			cprintf("%s\t%d\tRUNNABLE\t%d\t%d\t%d\t%d\n", p->name, p->pid, p->priority, p->stime, p->rtime, p->wtime);

	}
	cprintf("\n");
	release(&ptable.lock);

	return 22;
}
int chpr(int pid, int priority)
{
	struct proc* p;
	acquire(&ptable.lock);
	int val = 0;
	for(p = ptable.proc; p<&ptable.proc[NPROC]; p++){
		if (p->pid == pid) {
			val = p->priority;
			p->priority = priority;
			break;
		}
	}
	release(&ptable.lock);
	return val;
}
	int
waitx(int *wtime, int *rtime)
{
	int havekids, pid;
	struct proc *curproc = myproc(),*p;
	acquire(&ptable.lock);
	for(;;){
		// Scan through table looking for zombie children.
		havekids = 0;
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			if(p->parent != curproc)
				continue;
			havekids = 1;
			if(p->state == ZOMBIE){
				// Found one.

				// Added time field update, else same from wait system call
				*wtime = p->etime - p->stime - p->rtime ;

				*rtime = p->rtime;
				// same as wait
				pid = p->pid;
				kfree(p->kstack);
				p->kstack = 0;
				freevm(p->pgdir);
				p->state = UNUSED;
				 cprintf("%s\t%d\tSLEEPING \t%d\t%d\n",p->name,p->pid,p->priority,p->etime);
				p->pid = 0;
				p->killed = 0;
				p->name[0] = 0;
				p->parent = 0;
				release(&ptable.lock);
				return pid;
			}
		}

		// No point waiting if we don't have any children.
		if(!havekids || curproc->killed){
			release(&ptable.lock);
			return -1;
		}

		// Wait for children to exit.  (See wakeup1 call in proc_exit.)
		sleep(curproc, &ptable.lock);  //DOC: wait-sleep
	}
}
int getpinfo(struct procStat* ps, int pid)
{
	struct proc* p;
	int flag = 0;
	acquire(&ptable.lock);
	for(p = ptable.proc; p<&ptable.proc[NPROC]; ++p){
		if(p->pid == pid){
			flag = 1;
			break;
		}
	}
	release(&ptable.lock);

	if(!flag)
		return 0;
	if (p->pid != 0) {
    	for(int j = 0; j < 5; j++)
    		ps->ticks[j] = p->ticks[j];

    	ps->num_run = p->num_run;
    	ps->runtime = p->rtime;
    		// cprintf("%d %d",ps->runtime,p->rtime);
    	ps->pid = p->pid;
    	ps->current_queue = p->current_queue;
	}
	return 1;

}

void uprtime(){
	for(struct proc *p=ptable.proc; p<&ptable.proc[NPROC]; ++p){
		if(p->state == RUNNING){
			p->rtime++;
		}
	}
}
