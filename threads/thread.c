#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
/*새로운 sleep_list 생성*/
static struct list sleep_list;
static int64_t next_tick_to_awake;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/*next_tick_to_awake*/
static long long next_tick_to_awake;

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);
	//assert에 지정한 조건식이 거짓일 떄 프로그램을 중단하며 참일때는 프로그램을 계속 실행

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	//gdt(global decriptor table)
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */ 
	/*동기화를 위한 lock(mutex)과 scheduling을 위한 list의 초기화를 담당*/
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);
	/*list_init 설정해줌*/
	list_init (&sleep_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}
/*새로 만든 함수- thread_sleep*/
/*현재 thread가 idle thread인지를 체크하여 idle thread라면 중지한다.
 idel thread가 아니면 interrupt를 중지한 뒤 현재 thread를 sleep_list에 넣는 과정을 수행
 thread의 tick 값을 갱신하고, thread를 sleep_list에 넣고 block한다. 그리고 다시 interrupt 실행*/
void thread_sleep(int64_t ticks){
	struct thread *current = thread_current();

	
	
	if (current != idle_thread){ //idle_thread는 실행할 준비가 된 다른 스레드가 없을 때 실행
		enum intr_level old_level;
		/*인터럽트 중단*/
		old_level = intr_disable();
		current -> wakeup_tick = ticks;
		update_next_tick_to_awake(current->wakeup_tick);
		list_push_back(&sleep_list,&current->elem);
		thread_block();
		/*인터럽트 다시 실행*/
		intr_set_level(old_level);
	}
	
	
}

/*새로 만든 함수-thread_awake()*/
void thread_wakeup(int64_t ticks){

	next_tick_to_awake = INT64_MAX;
	struct list_elem *current_point = list_begin(&sleep_list);
	struct thread *t;

	/*sleep list 순환*/
	while(current_point!=list_end(&sleep_list)){
		t = list_entry(current_point,struct thread,elem);
	
	if(ticks >= t->wakeup_tick){
		current_point = list_remove(&t->elem);
		thread_unblock(t);
		}
	else {
		current_point=list_next(current_point);
		update_next_tick_to_awake(t->wakeup_tick); //next_tick이 바뀌었을 수 있으므로 업데이트해줌
		
		}
	}
	
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	/*idle thread를 만들고 */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	//idle 스레드를 만들고 맨 처음 ready queue에 들어간다.
	//semaphore를 1로 up 시켜 공유 자원의 접근을 가능하게 한다음 바로 block한다.
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	//thread_create(idle)에 disable했던 인터럽트 상태를 enable로 만듬
	//이제 스레드 스케줄링이 가능하다. 인터럽트(interrupt)가 가능하므로
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) { // 새 커널 스레드를 만들고 바로 ready queue에 넣어준다.
	struct thread *t;
	tid_t tid; //스레드 ID

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);

	/*내가 생성한 함수
	//new_thread 구조체를 생성후 tid(아마 새로 ready_list에 삽입할 thread)로 지정해준다
	struct thread *new_thread=tid;
	//t->priority: 실행중인 thread
	//new_thread->prioirty : 새로운 thread
	if (t->priority < new_thread->priority){
		thread_yield();
	}*/

	/*현재 실행되고 있는 thread와 새로 ready_list에 삽입할 thread의 우선순위를 비교한다. 
	만약 새로운 thread가 더 높은 우선순위 일 경우 CPU를 Yield한다.*/
	

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */

   /*현재 스레드를 절전 모드로 전환합니다. thread_unblock()에 의해 깨울 때까지 다시 예약되지 않습니다.
   인터럽트를 끈 상태에서 이 기능을 호출해야 합니다. 일반적으로 동기화 프리미티브 중 하나를 synch.h에 사용하는 것이 좋습니다.*/

void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   차단된 스레드 T를 실행 준비 상태로 전환합니다. 
   T가 차단되지 않은 경우 오류입니다. 
   (실행 중인 스레드를 준비하려면 thread_yield()를 사용합니다.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data.
   
   이 함수는 실행 중인 스레드를 선점하지 않습니다. 
   이것은 중요할 수 있습니다. 
   호출자가 인터럽트 자체를 비활성화한 경우 스레드를 원자적으로 차단 해제하고 다른 데이터를 업데이트할 수 있습니다.
    */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t)); // is_thread()는 주어진 포인터가 thread 구조체를 가르키는 올바른 thread 포인터인지를 검증한다.

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED); // thread t의 현재 상태가 blocked되어 있는지 확인한다.
	
	
	
	list_push_back (&ready_list, &t->elem); //ready_lis에 T를 가장 뒷줄에 다시 집어 넣는다.  list_push_back : 리스트의 맨 마지막 요소에 집어 넣음
	/*내가 수정한 코드
	//thread가 unblock 될때 우선순위 순으로 정렬되어 ready_list에 삽입되도록 수정
	list_insert_ordered(&ready_list,&t->elem, cmp_priority,NULL);*/

	
	
	t->status = THREAD_READY; // thread t의 현재 상태를 준비상태로 바꾼다.
	intr_set_level (old_level); //enable 한지 disable한지 판단한다.
}

void update_next_tick_to_awake(int64_t ticks){
	//다음으로 깨어나야 할 thread의 tick의 값을 최소 값으로 갱신
	next_tick_to_awake = (next_tick_to_awake > ticks) ? ticks : next_tick_to_awake;

	}
int64_t get_next_tick_to_awake(void){ //최소 tick의 값을 얻어오는 함수
	return next_tick_to_awake;
	}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) { //현재 실행 중인 스레드가 문제가 없는지를 확인한 다음 그 스레드를 가리키는 포인터를 반환
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t)); // thread인지 여부 체크, stack overflow를 체크한다.
	ASSERT (t->status == THREAD_RUNNING); // 스레드가 실행 중인지를 체크한다.

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) { //실행중인 thread의 id를 반환한다.
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) { /*현재 스레드의 일정을 취소하고 삭제합니다.  호출자에게 돌아가지 않습니다.*/
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
   //현재 실행중인 스레드들
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ()); //외부 인터럽트를 수행중이라면 종료, 외부 인터럽트는 인터럽트 당하면 안된다.

	old_level = intr_disable (); // 인터럽트를 disable 한다.
	//만약 현재 스레드가 idle 스레드가 아니라면 ready queue에 다시 담는다.
	//idle 스레드라면 담지 않는다. 어차피 static으로 선언되어 있어, 필요할 때 불러올 수 있다.
	if (curr != idle_thread)
		list_push_back (&ready_list, &curr->elem);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/*새로 생성한 함수(priority) - 현재 수행중인 스레드와 가장 높은 우선순위의 스레드의 우선순위를 비교하여 스케줄링*/
void test_max_priority(void){

}

/*새로 생성한 함수(priority) - 인자로 주어진 스레드들의 우선순위를 비교*/
bool cmp_priority(const struct list_elem *a,const struct list_elem *b, void *aux UNUSED){

}

/* Sets the current thread's priority to NEW_PRIORITY. */
/*현재 thread의 우선순위를 new_priority로 변경*/
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
}

/* Returns the current thread's priority. */
/*현재 thread의 우선순위를 반환*/
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) { 
	// 어떤 스레드들도 실행되고 있지 않을 때 실행되는 스레드, 맨 처음 thread_start()가 호출될때 ready queue에 먼저 들어가 있는다.
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current (); // 현재 들고 있는 스레드가 idle 밖에 없다.
	sema_up (idle_started); //semaphore의 값을 1로 만들어 줘 공유 자원의 공유(인터럽트) 가능

	for (;;) {
		/* Let someone else run. */
		intr_disable (); // 자기 자신(idle)을 block해주기 전까지 인터럽트 당하면 안되므로 먼저 disable한다.
		thread_block (); // 자기 자신을 block한다.

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables intefrrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
   //만든 스레드를 초기화 한다.
   //맨 처음 스레드의 상태는 blocked이다.
   // 커널 스택 포인터 rsp의 위치도 같이 정해준다. rsp의 값은 커널이 함수나 변수를 쌓을 수록 점점 작아진다.
static void
init_thread (struct thread *t, const char *name, int priority) { 
	ASSERT (t != NULL); //가리키는 공간이 비어있진 않고 (null은 아니고)
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX); // priority의 값은 제대로 박혀 있으며
	ASSERT (name != NULL); // 이름이 들어갈 공간은 있는지(디버그 용도)

	memset (t, 0, sizeof *t); //해당 메모리를 모두 0으로 초기화하고
	t->status = THREAD_BLOCKED; //맨 처음 스레드의 상태는 blocked
	strlcpy (t->name, name, sizeof t->name); //이름을 써 준다.
	//커널 스택 포인터의 위치를 지정해준다. 원래 스레드의 위치 t+4kb(1<<12)-포인터 변수 크기
	//커널 스택에 변수들이 쌓이면서 이 값은 작아진다.
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *); 
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}

}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

