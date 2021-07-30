/*
 * MacOSX amd64 fpu support
 * Mimic Plan 9 floating point support
 */

#include "lib9.h"

void
setfcr(u32 fcr)
{
	__asm__(
		"xorb	$0x3f, %%al\n\t"
		"movq	%%rax, (%%rsp)\n\t"
		"fwait\n\t"
		"fldcw	(%%rsp)\n\t"
		:	/* no output */
		: "al" (fcr)
	);
}

u32
getfcr(void)
{
	ulong fcr = 0;
	__asm__(
		"fwait\n\t"
		"fstcw	(%%rsp)\n\t"
		"movw	(%%rsp), %%ax\n\t"
		"andq	$0xffffff, %%rax\n\t"
		"xorb	$0x3f, %%al\n\t"
		: "=a" (fcr)
		: "rax" (fcr)
	);
	return fcr;
}

u32
getfsr(void)
{
	ulong fsr = -1;
	__asm__(
		"fwait\n\t"
		"fstsw	(%%rsp)\n\t"
		"movw	(%%rsp), %%ax\n\t"
		"andq	$0xffffff, %%rax\n\t"
		: "=a" (fsr)
		: "rax" (&fsr)
	);
	return fsr;
}

void
setfsr(u32 fsr)
{
	__asm__("fclex\n\t");
}

