// If the macro in the lib9 include dir doesn't work, we're out of luck.
	.file	"getcallerpc-MacOSX-386.s"
    .text
.globl _getcallerpc
_getcallerpc:
	pushq   %rbp
        movq    %rsp, %rbp
        movl    %edi, -4(%rbp)
        movq    8(%rbp), %rax
        popq    %rbp
        retq
