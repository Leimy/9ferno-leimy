#include "os.h"
#include "kernel.h"
#include <mp.h>
#include <libsec.h>

typedef struct State{
	int		seeded;
	u64		seed;
	DES3state	des3;
} State;
static State x917state;

static void
X917(uchar *rand, int nrand)
{
	int i, m, n8;
	u64 I, x;

	/* 1. Compute intermediate value I = Ek(time). */
	I = nsec();
	triple_block_cipher(x917state.des3.expanded, (uchar*)&I, 0); /* two-key EDE */

	/* 2. x[i] = Ek(I^seed);  seed = Ek(x[i]^I); */
	m = (nrand+7)/8;
	for(i=0; i<m; i++){
		x = I ^ x917state.seed;
		triple_block_cipher(x917state.des3.expanded, (uchar*)&x, 0);
		n8 = (nrand>8) ? 8 : nrand;
		memmove(rand, (uchar*)&x, n8);
		rand += 8;
		nrand -= 8;
		x ^= I;
		triple_block_cipher(x917state.des3.expanded, (uchar*)&x, 0);
		x917state.seed = x;
	}
}

static void
X917init(void)
{
	int n;
	uchar mix[128];
	uchar key3[3][8];
	u32 *ulp;

	ulp = (u32*)key3;
	for(n = 0; n < sizeof(key3)/sizeof(u32); n++)
		ulp[n] = truerand();
	setupDES3state(&x917state.des3, key3, nil);
	X917(mix, sizeof mix);
	x917state.seeded = 1;
}

void
genrandom(uchar *p, int n)
{
	_genrandomqlock();
	if(x917state.seeded == 0)
		X917init();
	X917(p, n);
	_genrandomqunlock();
}

QLock grandomlk;

void
_genrandomqlock(void)
{
	qlock(&grandomlk);
}


void
_genrandomqunlock(void)
{
	qunlock(&grandomlk);
}
