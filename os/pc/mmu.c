#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

#define	DATASEGM(p) 	{ 0xFFFF, SEGG|SEGB|(0xF<<16)|SEGP|SEGPL(p)|SEGDATA|SEGW }
#define	EXECSEGM(p) 	{ 0xFFFF, SEGG|SEGD|(0xF<<16)|SEGP|SEGPL(p)|SEGEXEC|SEGR }
#define	TSSSEGM(b,p)	{ ((b)<<16)|sizeof(Tss),\
			  ((b)&0xFF000000)|(((b)>>16)&0xFF)|SEGTSS|SEGPL(p)|SEGP }

Segdesc gdt[NGDT] =
{
[NULLSEG]	{ 0, 0},		/* null descriptor */
[KDSEG]		DATASEGM(0),		/* kernel data/stack */
[KESEG]		EXECSEGM(0),		/* kernel code */
[UDSEG]		DATASEGM(3),		/* user data/stack */
[UESEG]		EXECSEGM(3),		/* user code */
[TSSSEG]	TSSSEGM(0,0),		/* tss segment */
};

static void
taskswitch(ulong pdb, ulong stack)
{
	Tss *tss;

	tss = m->tss;
	tss->ss0 = KDSEL;
	tss->esp0 = stack;
	tss->ss1 = KDSEL;
	tss->esp1 = stack;
	tss->ss2 = KDSEL;
	tss->esp2 = stack;
	tss->cr3 = pdb;
	putcr3(pdb);
}

/* 
 * On processors that support it, we set the PTEGLOBAL bit in
 * page table and page directory entries that map kernel memory.
 * Doing this tells the processor not to bother flushing them
 * from the TLB when doing the TLB flush associated with a 
 * context switch (write to CR3).  Since kernel memory mappings
 * are never removed, this is safe.  (If we ever remove kernel memory
 * mappings, we can do a full flush by turning off the PGE bit in CR4,
 * writing to CR3, and then turning the PGE bit back on.) 
 *
 * See also mmukmap below.
 * 
 * Processor support for the PTEGLOBAL bit is enabled in devarch.c.
 */
static void
memglobal(void)
{
	int i, j;
	ulong *pde, *pte;

	/* only need to do this once, on bootstrap processor */
	if(m->machno != 0)
		return;

	if(!m->havepge)
		return;

	pde = m->pdb;
	for(i=512; i<1024; i++){	/* 512: start at entry for virtual 0x80000000 */
		if(pde[i] & PTEVALID){
			pde[i] |= PTEGLOBAL;
			if(!(pde[i] & PTESIZE)){
				pte = KADDR(pde[i]&~(BY2PG-1));
				for(j=0; j<1024; j++)
					if(pte[j] & PTEVALID)
						pte[j] |= PTEGLOBAL;
			}
		}
	}			
}

void
mmuinit(void)
{
	ulong x, *p;
	ushort ptr[3];

	memglobal();

	m->tss = malloc(sizeof(Tss));
	memset(m->tss, 0, sizeof(Tss));
	m->tss->iomap = 0xDFFF<<16;

	/*
	 * We used to keep the GDT in the Mach structure, but it
	 * turns out that that slows down access to the rest of the
	 * page.  Since the Mach structure is accessed quite often,
	 * it pays off anywhere from a factor of 1.25 to 2 on real
	 * hardware to separate them (the AMDs are more sensitive
	 * than Intels in this regard).  Under VMware it pays off
	 * a factor of about 10 to 100.
	 */

	memmove(m->gdt, gdt, sizeof gdt);
	x = (ulong)m->tss;
	m->gdt[TSSSEG].d0 = (x<<16)|sizeof(Tss);
	m->gdt[TSSSEG].d1 = (x&0xFF000000)|((x>>16)&0xFF)|SEGTSS|SEGPL(0)|SEGP;

	ptr[0] = sizeof(gdt)-1;
	x = (ulong)m->gdt;
	ptr[1] = x & 0xFFFF;
	ptr[2] = (x>>16) & 0xFFFF;
	lgdt(ptr);

	ptr[0] = sizeof(Segdesc)*256-1;
	x = IDTADDR;
	ptr[1] = x & 0xFFFF;
	ptr[2] = (x>>16) & 0xFFFF;
	lidt(ptr);

	/* make kernel text unwritable */
	for(x = KTZERO; x < (ulong)etext; x += BY2PG){
		p = mmuwalk(m->pdb, x, 2, 0);
		if(p == nil)
			panic("mmuinit");
		*p &= ~PTEWRITE;
	}

	taskswitch(PADDR(m->pdb),  (ulong)m + BY2PG);
	ltr(TSSSEL);
}




ulong*
mmuwalk(ulong* pdb, ulong va, int level, int create)
{
	ulong pa, *table;

	/*
	 * Walk the page-table pointed to by pdb and return a pointer
	 * to the entry for virtual address va at the requested level.
	 * If the entry is invalid and create isn't requested then bail
	 * out early. Otherwise, for the 2nd level walk, allocate a new
	 * page-table page and register it in the 1st level.
	 */
	table = &pdb[PDX(va)];
	if(!(*table & PTEVALID) && create == 0)
		return 0;

	switch(level){

	default:
		return 0;

	case 1:
		return table;

	case 2:
		if(*table & PTESIZE)
			panic("mmuwalk2: va %luX entry %luX\n", va, *table);
		if(!(*table & PTEVALID)){
			pa = PADDR(xspanalloc(BY2PG, BY2PG, 0));
			*table = pa|PTEWRITE|PTEVALID;
		}
		table = KADDR(PPN(*table));

		return &table[PTX(va)];
	}
}

static Lock mmukmaplock;

int
mmukmapsync(ulong va)
{
	Mach *mach0;
	ulong entry, *pte;

	mach0 = MACHP(0);

	ilock(&mmukmaplock);

	if((pte = mmuwalk(mach0->pdb, va, 1, 0)) == nil){
		iunlock(&mmukmaplock);
		return 0;
	}
	if(!(*pte & PTESIZE) && mmuwalk(mach0->pdb, va, 2, 0) == nil){
		iunlock(&mmukmaplock);
		return 0;
	}
	entry = *pte;

	if(!(m->pdb[PDX(va)] & PTEVALID))
		m->pdb[PDX(va)] = entry;

//	if(up && up->mmupdb){
//		((ulong*)up->mmupdb->va)[PDX(va)] = entry;
//		mmuflushtlb(up->mmupdb->pa);
//	}
//	else
		mmuflushtlb(PADDR(m->pdb));

	iunlock(&mmukmaplock);

	return 1;
}

ulong
mmukmap(ulong pa, ulong va, int size)
{
	Mach *mach0;
	ulong ova, pae, *table, pgsz, *pte, x;
	int pse, sync;

	mach0 = MACHP(0);
	if((mach0->cpuiddx & 0x08) && (getcr4() & 0x10))
		pse = 1;
	else
		pse = 0;
	sync = 0;

	pa = PPN(pa);
	if(va == 0)
		va = (ulong)KADDR(pa);
	else
		va = PPN(va);
	ova = va;

	pae = pa + size;
	ilock(&mmukmaplock);
	while(pa < pae){
		table = &mach0->pdb[PDX(va)];
		/*
		 * Possibly already mapped.
		 */
		if(*table & PTEVALID){
			if(*table & PTESIZE){
				/*
				 * Big page. Does it fit within?
				 * If it does, adjust pgsz so the correct end can be
				 * returned and get out.
				 * If not, adjust pgsz up to the next 4MB boundary
				 * and continue.
				 */
				x = PPN(*table);
				if(x != pa)
					panic("mmukmap1: pa %luX  entry %luX\n",
						pa, *table);
				x += 4*MB;
				if(pae <= x){
					pa = pae;
					break;
				}
				pgsz = x - pa;
				pa += pgsz;
				va += pgsz;

				continue;
			}
			else{
				/*
				 * Little page. Walk to the entry.
				 * If the entry is valid, set pgsz and continue.
				 * If not, make it so, set pgsz, sync and continue.
				 */
				pte = mmuwalk(mach0->pdb, va, 2, 0);
				if(pte && *pte & PTEVALID){
					x = PPN(*pte);
					if(x != pa)
						panic("mmukmap2: pa %luX entry %luX\n",
							pa, *pte);
					pgsz = BY2PG;
					pa += pgsz;
					va += pgsz;
					sync++;

					continue;
				}
			}
		}

		/*
		 * Not mapped. Check if it can be mapped using a big page -
		 * starts on a 4MB boundary, size >= 4MB and processor can do it.
		 * If not a big page, walk the walk, talk the talk.
		 * Sync is set.
		 *
		 * If we're creating a kernel mapping, we know that it will never
		 * expire and thus we can set the PTEGLOBAL bit to make the entry
	 	 * persist in the TLB across flushes.  If we do add support later for
		 * unmapping kernel addresses, see devarch.c for instructions on
		 * how to do a full TLB flush.
		 */
		if(pse && (pa % (4*MB)) == 0 && (pae >= pa+4*MB)){
			*table = pa|PTESIZE|PTEWRITE|PTEUNCACHED|PTEVALID;
			if((va&KZERO) && m->havepge)
				*table |= PTEGLOBAL;
			pgsz = 4*MB;
		}
		else{
			pte = mmuwalk(mach0->pdb, va, 2, 1);
			*pte = pa|PTEWRITE|PTEUNCACHED|PTEVALID;
			if((va&KZERO) && m->havepge)
				*pte |= PTEGLOBAL;
			pgsz = BY2PG;
		}
		pa += pgsz;
		va += pgsz;
		sync++;
	}
	iunlock(&mmukmaplock);

	/*
	 * If something was added
	 * then need to sync up.
	 */
	if(sync)
		mmukmapsync(ova);

	return pa;
}

void*
vmap(ulong pa, int size)
{
	pa = upamalloc(pa, size, 0);
	if(pa == 0)
		return nil;
	return KADDR(pa);
}

void
vunmap(void *va, int size)
{
	if(va != nil)
		upafree(PADDR(va), size);
}

s32
segflush(void*, u32)
{
	return 0;
}
