#include "lib9.h"
#include "draw.h"

static
void
doellipse(int cmd, Image *dst, Point *c, int xr, int yr, int thick, Image *src, Point *sp, int alpha, int phi, Drawop op)
{
	uchar *a;

	_setdrawop(dst->display, op);

	a = bufimage(dst->display, 1+4+4+2*4+4+4+4+2*4+2*4);
	if(a == 0){
		_drawprint(2, "image ellipse: %r\n");
		return;
	}
	a[0] = cmd;
	BP32INT(a+1, dst->id);
	BP32INT(a+5, src->id);
	BP32INT(a+9, c->x);
	BP32INT(a+13, c->y);
	BP32INT(a+17, xr);
	BP32INT(a+21, yr);
	BP32INT(a+25, thick);
	BP32INT(a+29, sp->x);
	BP32INT(a+33, sp->y);
	BP32INT(a+37, alpha);
	BP32INT(a+41, phi);
}

void
ellipse(Image *dst, Point c, int a, int b, int thick, Image *src, Point sp)
{
	doellipse('e', dst, &c, a, b, thick, src, &sp, 0, 0, SoverD);
}

void
ellipseop(Image *dst, Point c, int a, int b, int thick, Image *src, Point sp, Drawop op)
{
	doellipse('e', dst, &c, a, b, thick, src, &sp, 0, 0, op);
}

void
fillellipse(Image *dst, Point c, int a, int b, Image *src, Point sp)
{
	doellipse('E', dst, &c, a, b, 0, src, &sp, 0, 0, SoverD);
}

void
fillellipseop(Image *dst, Point c, int a, int b, Image *src, Point sp, Drawop op)
{
	doellipse('E', dst, &c, a, b, 0, src, &sp, 0, 0, op);
}

void
arc(Image *dst, Point c, int a, int b, int thick, Image *src, Point sp, int alpha, int phi)
{
	alpha |= 1<<31;
	doellipse('e', dst, &c, a, b, thick, src, &sp, alpha, phi, SoverD);
}

void
arcop(Image *dst, Point c, int a, int b, int thick, Image *src, Point sp, int alpha, int phi, Drawop op)
{
	alpha |= 1<<31;
	doellipse('e', dst, &c, a, b, thick, src, &sp, alpha, phi, op);
}

void
fillarc(Image *dst, Point c, int a, int b, Image *src, Point sp, int alpha, int phi)
{
	alpha |= 1<<31;
	doellipse('E', dst, &c, a, b, 0, src, &sp, alpha, phi, SoverD);
}

void
fillarcop(Image *dst, Point c, int a, int b, Image *src, Point sp, int alpha, int phi, Drawop op)
{
	alpha |= 1<<31;
	doellipse('E', dst, &c, a, b, 0, src, &sp, alpha, phi, op);
}
