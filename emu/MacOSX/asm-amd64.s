/*
 * these are the same as on the PC (eg, Linux)
 */

	.globl	_FPsave
_FPsave:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, %rax
	fstenv	(%rax)
	popq	%rbp
	ret

	.globl	_FPrestore
_FPrestore:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, %rax
	fldenv	(%rax)
	popq	%rbp
	ret

	.globl	__tas
__tas:
	movq	$1, %rax
	movq	%rdi, %rcx
	xchgq	%rax, 0(%rcx)
	ret

//	.globl	_FPsave
//_FPsave:
//	pushl	%ebp
//	movl	%esp, %ebp
//	movl	8(%ebp), %eax
//	fstenv	(%eax)
//	popl	%ebp
//	ret
//
//	.globl	_FPrestore
//_FPrestore:
//	pushl	%ebp
//	movl	%esp, %ebp
//	movl	8(%ebp), %eax
//	fldenv	(%eax)
//	popl	%ebp
//	ret
//
//	.globl	__tas
//__tas:
//	movl	$1, %eax
//	movl	4(%esp), %ecx
//	xchgl	%eax, 0(%ecx)
//	ret
