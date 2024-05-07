#include "x86.h"
#include "device.h"

extern TSS tss;
extern ProcessTable pcb[MAX_PCB_NUM];
extern int current;

extern int displayRow;
extern int displayCol;

void GProtectFaultHandle(struct StackFrame *sf);

void syscallHandle(struct StackFrame *sf);

void syscallWrite(struct StackFrame *sf);
void syscallPrint(struct StackFrame *sf);

void timerHandle(struct StackFrame *sf);
void syscallFork(struct StackFrame *sf);
void syscallSleep(struct StackFrame *sf);
void syscallExit(struct StackFrame *sf);

void irqHandle(struct StackFrame *sf)
{ // pointer sf = esp
	/* Reassign segment register */
	asm volatile("movw %%ax, %%ds" ::"a"(KSEL(SEG_KDATA)));
	/*XXX Save esp to stackTop */
	uint32_t tmpStackTop = pcb[current].stackTop;
	pcb[current].prevStackTop = pcb[current].stackTop;
	pcb[current].stackTop = (uint32_t)sf;

	switch (sf->irq)
	{
	case -1:
		break;
	case 0xd:
		GProtectFaultHandle(sf);
		break;
	case 0x20:
		timerHandle(sf);
		break;
	case 0x80:
		syscallHandle(sf);
		break;
	default:
		assert(0);
	}
	/*XXX Recover stackTop */
	pcb[current].stackTop = tmpStackTop;
}

void GProtectFaultHandle(struct StackFrame *sf)
{
	assert(0);
	return;
}

void timerHandle(struct StackFrame *sf)
{
	// TODO
	for (int i = 0; i < MAX_PCB_NUM; i++) {
		if (pcb[i].state == STATE_BLOCKED) {
			if (pcb[i].sleepTime > 0){
				pcb[i].sleepTime--;
			}
			if (pcb[i].sleepTime == 0){
				pcb[i].state = STATE_RUNNABLE;
			}
		}
	}	
	if (pcb[current].state == STATE_RUNNING && pcb[current].timeCount < MAX_TIME_COUNT) {
		pcb[current].timeCount++;
		return;
	}
	pcb[current].timeCount = 0;
	pcb[current].state = STATE_RUNNABLE;
	
	for (int i = (current + 1) % MAX_PCB_NUM; i != current; i = (i+1) % MAX_PCB_NUM) {
		if (pcb[i].state == STATE_RUNNABLE && i != 0) {
			current = i;
			break;
		}
	}
	pcb[current].state = STATE_RUNNING;
	do{
		uint32_t tmpStackTop = pcb[current].stackTop;
		pcb[current].stackTop = pcb[current].prevStackTop;
		tss.esp0 = (uint32_t)&(pcb[current].stackTop);
		asm volatile("movl %0, %%esp"::"m"(tmpStackTop)); // switch kernel stack
		asm volatile("popl %gs");
		asm volatile("popl %fs");
		asm volatile("popl %es");
		asm volatile("popl %ds");
		asm volatile("popal");
		asm volatile("addl $8, %esp");
		asm volatile("iret");
	}while(0);
}

void syscallHandle(struct StackFrame *sf)
{
	switch (sf->eax)
	{ // syscall number
	case 0:
		syscallWrite(sf);
		break; // for SYS_WRITE
	/*TODO Add Fork,Sleep... */
	case 1:
		syscallFork(sf);
		break;
	case 3:
		syscallSleep(sf);
		break;
	case 4:
		syscallExit(sf);
		break;
	default:
		break;
	}
}

void syscallWrite(struct StackFrame *sf)
{
	switch (sf->ecx)
	{ // file descriptor
	case 0:
		syscallPrint(sf);
		break; // for STD_OUT
	default:
		break;
	}
}

void syscallPrint(struct StackFrame *sf)
{
	int sel = sf->ds; // segment selector for user data, need further modification
	char *str = (char *)sf->edx;
	int size = sf->ebx;
	int i = 0;
	int pos = 0;
	char character = 0;
	uint16_t data = 0;
	asm volatile("movw %0, %%es" ::"m"(sel));
	for (i = 0; i < size; i++)
	{
		asm volatile("movb %%es:(%1), %0" : "=r"(character) : "r"(str + i));
		if (character == '\n')
		{
			displayRow++;
			displayCol = 0;
			if (displayRow == 25)
			{
				displayRow = 24;
				displayCol = 0;
				scrollScreen();
			}
		}
		else
		{
			data = character | (0x0c << 8);
			pos = (80 * displayRow + displayCol) * 2;
			asm volatile("movw %0, (%1)" ::"r"(data), "r"(pos + 0xb8000));
			displayCol++;
			if (displayCol == 80)
			{
				displayRow++;
				displayCol = 0;
				if (displayRow == 25)
				{
					displayRow = 24;
					displayCol = 0;
					scrollScreen();
				}
			}
		}
		// asm volatile("int $0x20"); //XXX Testing irqTimer during syscall
		// asm volatile("int $0x20":::"memory"); //XXX Testing irqTimer during syscall
	}

	updateCursor(displayRow, displayCol);
	// take care of return value
	return;
}

// TODO syscallFork ...
void syscallFork(struct StackFrame *sf){
	int index = 0;
	for (index = 0; index < MAX_PCB_NUM; index++) {
		if (pcb[index].state == STATE_DEAD) {
			break;
		}
	}
	if (index == MAX_PCB_NUM) {
		pcb[current].regs.eax = -1;
		return;
	}

	enableInterrupt();
	for (int i = 0; i < 0x100000; i++) {
		*(unsigned char *)(i + (index + 1) * 0x100000) = *(unsigned char *)(i + (current + 1) * 0x100000);
		asm volatile("int $0x20");
	}
	disableInterrupt();

	pcb[index].pid = index;
	pcb[index].prevStackTop = pcb[current].prevStackTop - (uint32_t)&(pcb[current]) + (uint32_t)&(pcb[index]);
	pcb[index].stackTop = pcb[current].stackTop - (uint32_t)&(pcb[current]) + (uint32_t)&(pcb[index]);
	pcb[index].state = STATE_RUNNABLE;
	pcb[index].timeCount = 0;
	pcb[index].sleepTime = 0;
	
	pcb[index].regs.eax = 0;
	pcb[index].regs.esp = pcb[current].regs.esp;
	pcb[index].regs.ebp = pcb[current].regs.ebp;
	pcb[index].regs.eip = pcb[current].regs.eip;
	pcb[index].regs.eflags = pcb[current].regs.eflags;
	pcb[index].regs.cs = pcb[current].regs.cs;
	pcb[index].regs.ds = pcb[current].regs.ds;
	pcb[index].regs.es = pcb[current].regs.es;
	pcb[index].regs.fs = pcb[current].regs.fs;
	pcb[index].regs.gs = pcb[current].regs.gs;
	pcb[index].regs.ss = pcb[current].regs.ss;
	pcb[index].regs.irq = pcb[current].regs.irq;
	pcb[index].regs.error = pcb[current].regs.error;
	pcb[index].regs.xxx = pcb[current].regs.xxx;

	pcb[current].regs.eax = index;
}
void syscallSleep(struct StackFrame *sf){
	pcb[current].state = STATE_BLOCKED;
	int find_flag = 0;
	for (int i = (current + 1) % MAX_PCB_NUM; i != current; i = (i+1) % MAX_PCB_NUM){
		if (pcb[i].state == STATE_RUNNABLE && i != 0){
			find_flag = 1;
			current = i;
			break;
		}
	}
	if (!find_flag){
		current = 0;
	}
	pcb[current].state = STATE_RUNNING;
	do{
		uint32_t tmpStackTop = pcb[current].stackTop;
		pcb[current].stackTop = pcb[current].prevStackTop;
		tss.esp0 = (uint32_t)&(pcb[current].stackTop);
		asm volatile("movl %0, %%esp"::"m"(tmpStackTop)); // switch kernel stack
		asm volatile("popl %gs");
		asm volatile("popl %fs");
		asm volatile("popl %es");
		asm volatile("popl %ds");
		asm volatile("popal");
		asm volatile("addl $8, %esp");
		asm volatile("iret");
	}while(0);
}
void syscallExit(struct StackFrame *sf){
	pcb[current].state = STATE_DEAD;
	int find_flag = 0;
	for (int i = (current + 1) % MAX_PCB_NUM; i != current; i = (i+1) % MAX_PCB_NUM){
		if (pcb[i].state == STATE_RUNNABLE && i != 0){
			find_flag = 1;
			current = i;
			break;
		}
	}
	if (!find_flag){
		current = 0;
	}
	pcb[current].state = STATE_RUNNING;
	do{
		uint32_t tmpStackTop = pcb[current].stackTop;
		pcb[current].stackTop = pcb[current].prevStackTop;
		tss.esp0 = (uint32_t)&(pcb[current].stackTop);
		asm volatile("movl %0, %%esp"::"m"(tmpStackTop)); // switch kernel stack
		asm volatile("popl %gs");
		asm volatile("popl %fs");
		asm volatile("popl %es");
		asm volatile("popl %ds");
		asm volatile("popal");
		asm volatile("addl $8, %esp");
		asm volatile("iret");
	}while(0);
};