.text
.globl tigermain
.type tigermain, @function
tigermain:
push %ebp
movl %esp, %ebp
L2:
movl $L0, %ecx
push %ecx
call print
movl %eax, %ecx
movl $4, %edx
movl %esp, %eax
addl %edx, %eax
movl %eax, %esp
movl %ecx, %eax
jmp L1
L1:

leave
ret

.section .rodata
L0:
#.asciz "hello world!"
.string "   hello world"

