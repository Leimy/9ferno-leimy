	.file	"getcallerpc-MacOSX-amd64.s"
	.text
.globl _getcallerpc
_getcallerpc:
	pushq   %rbp
        movq    %rsp, %rbp
        movl    %edi, -4(%rbp)
        movq    8(%rbp), %rax
        popq    %rbp
        retq
