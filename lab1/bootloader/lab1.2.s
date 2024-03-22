# TODO: This is lab1.2
/* Protected Mode Hello World */
.code16
	
.global start
start:
	movw %cs, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	# TODO:关闭中断
	cli # 关闭中断

	# 启动A20总线
	inb $0x92, %al 
	orb $0x02, %al
	outb %al, $0x92

	# 加载GDTR
	data32 addr32 lgdt gdtDesc # loading gdtr, data32, addr32

	# TODO：设置CR0的PE位（第0位）为1
	movl %cr0, %eax
	orb $0x01, %al
	movl %eax, %cr0

	# 长跳转切换至保护模式
	data32 ljmp $0x08, $start32 # reload code segment selector and ljmp to start32, data32

.code32
start32:
	movw $0x10, %ax # setting data segment selector
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %ss
	movw $0x18, %ax # setting graphics data segment selector
	movw %ax, %gs
	
	movl $0x8000, %eax # setting esp
	movl %eax, %esp
	# TODO:输出Hello World
	pushl $13     # pushing the size to print into stack
	pushl $message # pushing the address of message into stack
	calll displayStr # calling the display function	


loop32:
	jmp loop32

message:
	.string "Hello, World!\n\0"
	
displayStr:
	movl 4(%esp), %ebx # 字符串地址
	movl 8(%esp), %ecx # 字符串长度
	movl $0, %edi        # 显示位置
	movb $0x0c, %ah      # 黑底红字
nextChar:
	movb (%ebx), %al # 读取字符
	movw %ax, %gs:(%edi) # 写入显存
	addl $2, %edi # 显示位置+2
	incl %ebx # 字符串地址+1以指向下一个字符
	loopnz nextChar # 循环
	ret


.p2align 2
gdt: # 8 bytes for each table entry, at least 1 entry
	# .word limit[15:0],base[15:0]
	# .byte base[23:16],(0x90|(type)),(0xc0|(limit[19:16])),base[31:24]
	# GDT第一个表项为空
	.word 0,0 
	.byte 0,0,0,0 

	# TODO：code segment entry
	.word 0xffff, 0 # 段长度(segment length), 段基址(segment base),bit0 - bit15
	.byte 0, 0x9a, 0xcf, 0 # 段基址(segment base) Access Byte (flags) 段长度 flags标志位


	# TODO：data segment entry
	.word 0xffff, 0
	.byte 0, 0x92, 0xcf, 0 # 同上，标志位可能不同

	# TODO：graphics segment entry
	.word 0xffff, 0x8000 # 段长度, 段基址
	.byte 0x0b, 0x92, 0xcf, 0 # 段基址, Access Byte, 段长度, flags

gdtDesc: 
	.word (gdtDesc - gdt -1) 
	.long gdt 

