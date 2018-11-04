#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "version.h"

#define	MAXCONF		32

Mach *m = (Mach*)MACHADDR;
Proc *up = 0;
Vectorpage	*page0 = (Vectorpage*)KZERO;	/* doubly-mapped to AIVECADDR */
Conf conf;

extern ulong kerndate;
extern int cflag;
extern int main_pool_pcnt;
extern int heap_pool_pcnt;
extern int image_pool_pcnt;
ulong cpuidlecount;

char *confname[MAXCONF];
char *confval[MAXCONF];
int nconf;

void addconf(char *, char *);
void	eepromscan(void);
char*	getconf(char*);

void
doc(char *m)
{
	USED(m);
	print("%s...\n", m);
}

void
idoc(char *m)
{
	uartputs(m, strlen(m)); //xdelay(1);
}

static void
poolsizeinit(void)
{
	ulong nb;

	nb = conf.npage*BY2PG;
	poolsize(mainmem, (nb*main_pool_pcnt)/100, 0);
	poolsize(heapmem, (nb*heap_pool_pcnt)/100, 0);
	poolsize(imagmem, (nb*image_pool_pcnt)/100, 1);
}

static void
serialconsole(void)
{
	char *p;
	int port, baud;

	p = getconf("console");
	if(p == nil)
		p = "0";
	if(p != nil){
		port = strtol(p, nil, 0);
		baud = 38400;
		p = getconf("baud");
		if(p != nil){
			baud = strtol(p, nil, 0);
			if(baud < 38400)
				baud = 38400;
		}
		uartspecial(port, baud, &kbdq, &printq, kbdcr2nl);
	}
}

static char *hello = "Inferno\n";

void
main(void)
{
	memset(edata, 0, end-edata);		/* clear the BSS */
	memset(m, 0, sizeof(Mach));	/* clear the mach struct */
	conf.nmach = 1;
	archreset();
	idoc(hello);
if(0){
	putcclkcfg(0);	/* leave turbo mode */
	COREREG->cccr = (COREREG->cccr & ~(7<<7)) | (4<<7);	/* turbo mode multiplier=2 */
	putcclkcfg(1);	/* enter turbo mode */
}
	quotefmtinstall();
	idoc("confinit...\n");
	confinit();
	idoc("xinit...\n");
	xinit();
	idoc("mmuinit...\n");
	mmuinit();
	poolsizeinit();
	poolinit();
	idoc("trapinit...\n");
	trapinit();
//	dmareset();
	idoc("printinit...\n");
	printinit();
	idoc("uartinstall...\n");
	uartinstall();
	eepromscan();
	doc("clockinit");
	clockinit();
	doc("screeninit");
//	screeninit();
	doc("procinit");
	procinit();
//	cpuidprint();
	doc("links");
	links();
	doc("chandevreset");
	chandevreset();

	eve = strdup("inferno");

	serialconsole();
	kbdinit();

	print("%ld MHz id %8.8lux\n", (m->cpuhz+500000)/1000000, getcpuid());
	print("\nInferno %s\n", VERSION);
	print("Vita Nuova\n");
	print("conf %s (%lud) jit %d\n\n",conffile, kerndate, cflag);
print("pmcr=%8.8lux pwer=%8.8lux prer=%8.8lux pfer=%8.8lux pedr=%8.8lux pcfr=%8.8lux rcsr=%8.8lux\n",
	PMGRREG->pmcr, PMGRREG->pwer, PMGRREG->prer, PMGRREG->pfer, PMGRREG->pedr, PMGRREG->pcfr,
	PMGRREG->rcsr);
print("cccr=%8.8lux cken=%8.8lux oscc=%8.8lux\n", COREREG->cccr, COREREG->cken, COREREG->oscc);
print("msc0=%8.8lux mcs1=%8.8lux mcs2=%8.8lux\n", MEMCFGREG->msc0, MEMCFGREG->msc1, MEMCFGREG->msc2);
print("clkcfg=%8.8lux\n", getcclkcfg());

	userinit();
	schedinit();
}

void
reboot(void)
{
	exit(0);
}

void
halt(void)
{
	spllo();
	print("cpu halted\n");
	for(;;){
		/* nothing to do */
	}
}

Conf	conf;

void
addconf(char *name, char *val)
{
	if(nconf >= MAXCONF)
		return;
	confname[nconf] = name;
	confval[nconf] = val;
	nconf++;
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return confval[i];
	return 0;
}

void
confinit(void)
{
	ulong base;

	archconfinit();

	base = PGROUND((ulong)end);
	conf.base0 = base;

	conf.base1 = 0;
	conf.npage1 = 0;

	conf.npage0 = (conf.topofmem - base)/BY2PG;

	conf.npage = conf.npage0 + conf.npage1;
	conf.ialloc = (((conf.npage*(main_pool_pcnt))/100)/2)*BY2PG;

	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	conf.nmach = 1;

}

void
init0(void)
{
	Osenv *o;
	char buf[2*KNAMELEN];

	up->nerrlab = 0;

	spllo();

	if(waserror())
		panic("init0 %r");
	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	o = up->env;
	o->pgrp->slash = namec("#/", Atodir, 0, 0);
	cnameclose(o->pgrp->slash->name);
	o->pgrp->slash->name = newcname("/");
	o->pgrp->dot = cclone(o->pgrp->slash);

	chandevinit();

	if(!waserror()){
		ksetenv("cputype", "arm", 0);
		snprint(buf, sizeof(buf), "arm %s", conffile);
		ksetenv("terminal", buf, 0);
		poperror();
	}

	poperror();

	disinit("/osinit.dis");
}

void
userinit(void)
{
	Proc *p;
	Osenv *o;

	p = newproc();
	o = p->env;

	o->fgrp = newfgrp(nil);
	o->pgrp = newpgrp();
	o->egrp = newegrp();
	kstrdup(&o->user, eve);

	strcpy(p->text, "interp");

	p->fpstate = FPINIT;

	/*
	 * Kernel Stack
	 *
	 * N.B. The -12 for the stack pointer is important.
	 *	4 bytes for gotolabel's return PC
	 */
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->kstack+KSTACK-8;

	ready(p);
}

void
exit(int inpanic)
{
	up = 0;

	/* Shutdown running devices */
	chandevshutdown();

	if(inpanic && 0){
		print("Hit the reset button\n");
		for(;;)
			clockpoll();
	}
	archreboot();
}

static void
linkproc(void)
{
	spllo();
	if (waserror())
		print("error() underflow: %r\n");
	else
		(*up->kpfun)(up->arg);
	pexit("end proc", 1);
}

void
kprocchild(Proc *p, void (*func)(void*), void *arg)
{
	p->sched.pc = (ulong)linkproc;
	p->sched.sp = (ulong)p->kstack+KSTACK-8;

	p->kpfun = func;
	p->arg = arg;
}

void
idlehands(void)
{
	cpuidlecount++;
	INTRREG->iccr = 1;	/* only unmasked interrupts will stop idle mode */
	idle();
}

/* stubs */
void
setfsr(ulong)
{
}

ulong
getfsr()
{
	return 0;
}

void
setfcr(ulong)
{
}

ulong
getfcr()
{
	return 0;
}

void
fpinit(void)
{
}

void
FPsave(void*)
{
}

void
FPrestore(void*)
{
}
