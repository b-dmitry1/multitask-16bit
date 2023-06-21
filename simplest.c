#include <stdio.h>
#include <string.h>

#define TIMER_INTR		0x08
#define SLEEP_INTR		0x70
#define BIOS_TIMER_INTR		0x78

#define FLAGS_INT_ENABLE	0x200

typedef struct
{
	// CPU regs to be saved/restored
	unsigned int bp, di, si, ds, es;
	unsigned int dx, cx, bx, ax;
	// Task's next instruction address and flags
	unsigned int ip, cs, flags;
} cpu_regs_t;

typedef struct task_struct
{
	// Task's stack pointer
	unsigned int sp;
	// Pointer to a next task (linked list)
	struct task_struct *next;
} task_t;

task_t *first_task = NULL;
task_t *current_task = NULL;

// "Scheduler"
void schedule_next_task(void)
{
	current_task = current_task->next;
	if (current_task == NULL)
	{
		current_task = first_task;
	}
}

// Sleep / voluntary multitasking
void sleep(void)
{
	asm int SLEEP_INTR
}

void interrupt sleep_isr(void)
{
	unsigned int sp_reg;

	// Save current task's stack pointer (saved in BP by compiler)
	asm mov sp_reg, bp
	current_task->sp = sp_reg;

	schedule_next_task();

	// Restore next task's stack pointer (via BP)
	sp_reg = current_task->sp;
	asm mov bp, sp_reg
}

// Periodic timer routine / preemptive multitasking
void interrupt timer_isr(void)
{
	unsigned int sp_reg;

	// Save current task's stack pointer (saved in BP by compiler)
	asm mov sp_reg, bp
	current_task->sp = sp_reg;

	schedule_next_task();

	// Restore next task's stack pointer (via BP)
	sp_reg = current_task->sp;
	asm mov bp, sp_reg

	// Call default BIOS timer routine
	asm int BIOS_TIMER_INTR
}

void add_task(task_t *task, void (*func)(void),
	unsigned char *stack, unsigned int stack_size)
{
	cpu_regs_t *regs;

	// Set task's stack pointer to a top of the stack area
	task->sp = (unsigned int)stack;
	task->sp += stack_size;

	// "Push" initial register values to a task's stack
	task->sp -= sizeof(cpu_regs_t);

	regs = (cpu_regs_t *)&stack[stack_size - sizeof(cpu_regs_t)];
	regs->flags = FLAGS_INT_ENABLE;
	regs->cs = _CS;
	regs->ip = (unsigned int)func;
	regs->ds = _DS;
	regs->es = _ES;
	regs->bp = task->sp - 2;

	// Add to a task list
	task->next = first_task;
	first_task = task;
}

void get_isr_vector(unsigned char number, unsigned int *cs, unsigned int *ip)
{
	unsigned far *intr = (unsigned far *)0;
	*ip = intr[number * 2];
	*cs = intr[number * 2 + 1];
}

void set_isr_vector(unsigned char number, unsigned int cs, unsigned int ip)
{
	unsigned far *intr = (unsigned far *)0;
	intr[number * 2] = ip;
	intr[number * 2 + 1] = cs;
}

void set_isr_vectors(void)
{
	unsigned int cs, ip;

	// Set BIOS's timer isr vector to a new position
	get_isr_vector(TIMER_INTR, &cs, &ip);
	set_isr_vector(BIOS_TIMER_INTR, cs, ip);

	// Set timer isr / sleep syscall vectors
	set_isr_vector(TIMER_INTR, _CS, (unsigned int)timer_isr);
	set_isr_vector(SLEEP_INTR, _CS, (unsigned int)sleep_isr);
}

void start_scheduler(void)
{
	unsigned int sp_reg;

	asm cli

	set_isr_vectors();

	// Select first task
	current_task = first_task;
	sp_reg = current_task->sp;

	// Start task the same way as it is done
	// in timer or sleep ISR
	asm {
		mov sp, sp_reg
		pop bp
		pop di
		pop si
		pop ax
		pop ax
		pop dx
		pop cx
		pop bx
		pop ax
		iret
	}
}




//////////////////////////////////////////////////////////////////////////
// User's part
//////////////////////////////////////////////////////////////////////////

unsigned char stack1[2048];
unsigned char stack2[2048];
unsigned char stack3[2048];

task_t task1_data;
task_t task2_data;
task_t task3_data;

// Task 1 never voluntarily gives control
// So preemptive multitasking is the only choice
void task1(void)
{
	volatile int w;
	for (;;)
	{
		printf("Task %d  ", 1);
		for (w = 0; w < 10000; w++);
	}
}

// Task 2 periodically voluntarily gives control
// to other tasks
void task2(void)
{
	for (;;)
	{
		printf("Task %d  ", 2);
		sleep();
	}
}

// Task 3 uses more CPU time than task 2 and periodically
// voluntarily gives control to other tasks
void task3(void)
{
	volatile int w;
	for (;;)
	{
		printf("Task %d  ", 3);
		for (w = 0; w < 10000; w++);
		sleep();
	}
}

void main(void)
{
	add_task(&task1_data, task1, stack1, sizeof(stack1));
	add_task(&task2_data, task2, stack2, sizeof(stack2));
	add_task(&task3_data, task3, stack3, sizeof(stack3));

	start_scheduler();
}
