#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"kernel.h"

typedef	struct Fid	Fid;
typedef	struct Export	Export;
typedef	struct Exq	Exq;
typedef	struct Uqid Uqid;

enum
{
	Nfidhash	= 32,
	Nqidhash = 32,
	QIDMASK = ((s64)1<<48)-1,
	MAXFDATA	= 8192,
	MAXRPCDEF		= IOHDRSZ+MAXFDATA,	/* initial/default */
	MAXRPCMAX	= IOHDRSZ+64*1024,	/* most every allowed */
	MSGHDRSZ	= BIT32SZ+BIT8SZ+BIT16SZ
};

struct Export
{
	Lock;
	Ref	r;
	Exq*	work;
	Lock	fidlock;
	Fid*	fid[Nfidhash];
	QLock	qidlock;
	Uqid*	qids[Nqidhash];
	u32	pathgen;
	Chan*	io;
	Chan*	root;
	Pgrp*	pgrp;
	Egrp*	egrp;
	Fgrp*	fgrp;
	s32	async;
	s32	readonly;
	s32	msize;
	char*	user;
};

struct Fid
{
	Fid*	next;
	Fid**	last;
	Chan*	chan;
	s32	fid;
	s32	ref;		/* fcalls using the fid; locked by Export.Lock */
	s64	offset;	/* last offset used (within directory) */
	s32	attached;	/* fid attached or cloned but not clunked */
	Uqid*	qid;	/* generated qid */
};

struct Uqid
{
	Ref;
	s32	type;
	s32	dev;
	s64	oldpath;
	s64	newpath;
	Uqid*	next;
};

struct Exq
{
	Lock;
	s32	busy;	/* fcall in progress */
	s32	finished;	/* will do no more work on this request or flushes */
	Exq*	next;
	s32	shut;		/* has been noted for shutdown */
	Exq*	flush;	/* queued flush requests */
	Exq*	flusht;	/* tail of flush queue */
	Export*	export;
	Proc*	slave;
	Fcall	in, out;
	uchar*	buf;
	s32	bsize;
};

struct
{
	Lock	l;
	QLock	qwait;
	Rendez	rwait;
	Exq	*head;		/* work waiting for a slave */
	Exq	*tail;
}exq;

static void	exshutdown(Export*);
static int	exflushed(Export*, Exq*);
static void	exslave(void*);
static void	exfree(Export*);
static void	exfreeq(Exq*);
static void	exportproc(void*);
static void	exreply(Exq*, char*);
static int	exisroot(Export*, Chan*);
static Uqid*	uqidalloc(Export*, Chan*);
static void		freeuqid(Export*, Uqid*);

static char*	Exversion(Export*, Fcall*, Fcall*);
static char*	Exauth(Export*, Fcall*, Fcall*);
static char*	Exattach(Export*, Fcall*, Fcall*);
static char*	Exclunk(Export*, Fcall*, Fcall*);
static char*	Excreate(Export*, Fcall*, Fcall*);
static char*	Exopen(Export*, Fcall*, Fcall*);
static char*	Exread(Export*, Fcall*, Fcall*);
static char*	Exremove(Export*, Fcall*, Fcall*);
static char*	Exstat(Export*, Fcall*, Fcall*);
static char*	Exwalk(Export*, Fcall*, Fcall*);
static char*	Exwrite(Export*, Fcall*, Fcall*);
static char*	Exwstat(Export*, Fcall*, Fcall*);

static char	*(*fcalls[Tmax])(Export*, Fcall*, Fcall*);

static char	Enofid[]   = "no such fid";
static char	Eseekdir[] = "can't seek on a directory";
static char	Eopen[]	= "walk of open fid";
static char	Emode[] = "open/create -- unknown mode";
static char	Edupfid[]	= "fid in use";
static char	Eaccess[] = "read/write -- not open in suitable mode";
static char	Ecount[] = "read/write -- count too big";
int	exdebug = 0;

int
export(int fd, char *dir, int async)
{
	Chan *c, *dc;
	Pgrp *pg;
	Egrp *eg;
	Export *fs;

	if(waserror())
		return -1;
	c = fdtochan(up->env->fgrp, fd, ORDWR, 1, 1);
	poperror();

	if(waserror()){
		cclose(c);
		return -1;
	}
	dc = namec(dir, Atodir, 0, 0);
	poperror();

	fs = malloc(sizeof(Export));
	if(fs == nil){
		cclose(c);
		cclose(dc);
		error(Enomem);
	}

	fs->r.ref = 1;
	pg = up->env->pgrp;
	fs->pgrp = pg;
	incref(pg);
	eg = up->env->egrp;
	fs->egrp = eg;
	if(eg != nil)
		incref(eg);
	fs->fgrp = newfgrp(nil);
	kstrdup(&fs->user, up->env->user);
	fs->root = dc;
	fs->io = c;
	fs->pathgen = 0;
	fs->msize = 0;
	c->flag |= CMSG;
	fs->async = async;

	if(async){
		if(waserror())
			return -1;
		kproc("exportfs", exportproc, fs, 0);
		poperror();
	}else
		exportproc(fs);

	return 0;
}

static void
exportinit(void)
{
	lock(&exq.l);
	if(fcalls[Tversion] != nil) {
		unlock(&exq.l);
		return;
	}
	fcalls[Tversion] = Exversion;
	fcalls[Tauth] = Exauth;
	fcalls[Tattach] = Exattach;
	fcalls[Twalk] = Exwalk;
	fcalls[Topen] = Exopen;
	fcalls[Tcreate] = Excreate;
	fcalls[Tread] = Exread;
	fcalls[Twrite] = Exwrite;
	fcalls[Tclunk] = Exclunk;
	fcalls[Tremove] = Exremove;
	fcalls[Tstat] = Exstat;
	fcalls[Twstat] = Exwstat;
	unlock(&exq.l);
}

static int
exisroot(Export *fs, Chan *c)
{
	return eqchan(fs->root, c, 1);
}

static int
exreadn(Chan *c, void *buf, int n)
{
	int nr, t;

	if(waserror())
		return -1;
	for(nr = 0; nr < n;){
		t = devtab[c->type]->read(c, (char*)buf+nr, n-nr, 0);
		if(t <= 0)
			break;
		nr += t;
	}
	poperror();
	return nr;
}

static int
exreadmsg(Chan *c, void *a, uint n)
{
	int m, len;
	uchar *buf;

	buf = a;
	m = exreadn(c, buf, BIT32SZ);
	if(m < BIT32SZ){
		if(m < 0)
			return -1;
		return 0;
	}
	len = GBIT32(buf);
	if(len <= BIT32SZ || len > n){
		kwerrstr("bad length in Styx message header");
		return -1;
	}
	len -= BIT32SZ;
	m = exreadn(c, buf+BIT32SZ, len);
	if(m < len){
		if(m < 0)
			return -1;
		return 0;
	}
	return BIT32SZ+m;
}

static void
exportproc(void *a)
{
	Exq *q;
	int async, msize;
	int n, type;
	Export *fs = a;

	exportinit();

	for(;;){

		msize = fs->msize;
		if(msize == 0)
			msize = MAXRPCDEF;
		for(n=0;; n++){	/* we don't use smalloc, to avoid memset */
			q = mallocz(sizeof(*q)+msize, 0);
			if(q != nil || n > 6000)
				break;
			if(n%600 == 0)
				print("exportproc %d: waiting for memory (%d) for request\n", up->pid, msize);
			tsleep(&up->sleep, return0, nil, 100);
		}
		if(q == nil){
			kwerrstr("out of memory: read request");
			n = -1;
			break;
		}
		memset(q, 0, sizeof(*q));
		q->buf = (uchar*)q + sizeof(*q);
		q->bsize = msize;

		n = exreadmsg(fs->io, q->buf, msize);	/* TO DO: avoid copy */
		if(n <= 0)
			break;
		if(convM2S(q->buf, n, &q->in) != n){
			kwerrstr("bad T-message");
			n = -1;
			break;
		}
		type = q->in.type;
		if(type < Tversion || type >= Tmax || type&1 || type == Terror){
			kwerrstr("invalid T-message type %d", type);
			n = -1;
			break;
		}

		if(exdebug)
			print("export %d <- %F\n", up->pid, &q->in);

		q->out.type = type+1;
		q->out.tag = q->in.tag;

		q->export = fs;
		incref(&fs->r);

		if(fs->readonly){
			switch(type){
			case Topen:
				if((q->in.mode & (ORCLOSE|OTRUNC|3)) == OREAD)
					break;
				/* FALL THROUGH */
			case Tcreate:
			case Twrite:
			case Tremove:
			case Twstat:
				q->out.type = Rerror;
				q->out.ename = "file system read only";
				exreply(q, "exportproc");
				exfreeq(q);
				continue;
			}
		}

		if(q->in.type == Tflush){
			if(exflushed(fs, q)){
				/* not yet started or not found (flush arrived after reply); we reply */
				if(exdebug)
					print("export: flush %d\n", q->in.oldtag);
				exreply(q, "exportproc");
				exfreeq(q);
			}
			continue;
		}

		lock(&exq.l);
		if(exq.head == nil)
			exq.head = q;
		else
			exq.tail->next = q;
		q->next = nil;
		exq.tail = q;
		unlock(&exq.l);
		if(exq.qwait.head == nil)
			kproc("exslave", exslave, nil, 0);
		wakeup(&exq.rwait);
	}

	if(exdebug){
		if(n < 0)
			print("exportproc %d shut down: %s\n", up->pid, up->env->errstr);
		else
			print("exportproc %d shut down\n", up->pid);
	}

	free(q);
	exshutdown(fs);
	async = fs->async;
	exfree(fs);

	if(async)
		pexit("mount shut down", 0);
}

static int
exflushed(Export *fs, Exq *fq)
{
	Exq *q, **last;
	ulong pid;

	/* not yet started? */
	lock(&exq.l);
	for(last = &exq.head; (q = *last) != nil; last = &q->next)
		if(q->export == fs && q->in.tag == fq->in.oldtag){
			*last = q->next;
			unlock(&exq.l);
			/* not yet started: discard, and Rflush */
			exfreeq(q);
			return 1;
		}
	unlock(&exq.l);

	/* tricky case: in progress */
	lock(fs);
	for(q = fs->work; q != nil; q = q->next)
		if(q->in.tag == fq->in.oldtag){
			pid = 0;
			lock(q);
			if(q->finished){
				/* slave replied and emptied its flush queue; we can Rflush now */
				unlock(q);
				return 1;
			}
			/* append to slave's flush queue */
			fq->next = nil;
			if(q->flush != nil)
				q->flusht->next = fq;
			else
				q->flush = fq;
			q->flusht = fq;
			if(q->busy){
				pid = q->slave->pid;
				swiproc(q->slave, 0);
			}
			unlock(q);
			unlock(fs);
			if(exdebug && pid)
				print("export: swiproc %ld to flush %d\n", pid, fq->in.oldtag);
			return 0;
		}
	unlock(fs);

	/* not found */
	return 1;
}

static void
exfreeq(Exq *q)
{
	Exq *fq;

	while((fq = q->flush) != nil){
		q->flush = fq->next;
		exfree(fq->export);
		free(fq);
	}
	exfree(q->export);
	free(q);
}

static void
exshutdown(Export *fs)
{
	Exq *q, **last;

	/* work not started */
	lock(&exq.l);
	for(last = &exq.head; (q = *last) != nil;)
		if(q->export == fs){
			*last = q->next;
			exfreeq(q);
		}else
			last = &q->next;
	unlock(&exq.l);

	/* tell slaves to abandon work in progress */
	lock(fs);
	while((q = fs->work) != nil){
		fs->work = q->next;
		lock(q);
		q->shut = 1;
		swiproc(q->slave, 0);	/* whether busy or not */
		unlock(q);
	}
	unlock(fs);
}

static void
exfreefids(Export *fs)
{
	Fid *f, *n;
	int i;

	for(i = 0; i < Nfidhash; i++){
		for(f = fs->fid[i]; f != nil; f = n){
			n = f->next;
			f->attached = 0;
			if(f->ref == 0) {
				if(f->chan != nil)
					cclose(f->chan);
				freeuqid(fs, f->qid);
				free(f);
			} else
				print("exfreefids: busy fid\n");
		}
	}
}

static void
exfree(Export *fs)
{
	if(exdebug)
		print("export p/s %d free %p ref %d\n", up->pid, fs, fs->r.ref);
	if(decref(&fs->r) != 0)
		return;
	closepgrp(fs->pgrp);
	closeegrp(fs->egrp);
	closefgrp(fs->fgrp);
	cclose(fs->root);
	cclose(fs->io);
	exfreefids(fs);
	free(fs->user);
	free(fs);
}

static int
exwork(void*)
{
	return exq.head != nil;
}

static void
exslave(void*)
{
	Export *fs;
	Exq *q, *t, *fq, **last;
	char *err;
	int nstat;

	for(;;){
		qlock(&exq.qwait);
		if(waserror()){
			qunlock(&exq.qwait);
			continue;
		}
		sleep(&exq.rwait, exwork, nil);
		poperror();

		lock(&exq.l);
		q = exq.head;
		if(q == nil) {
			unlock(&exq.l);
			qunlock(&exq.qwait);
			continue;
		}
		exq.head = q->next;

		qunlock(&exq.qwait);

		/*
		 * put the job on the work queue before it's
		 * visible as off of the head queue, so it's always
		 * findable for flushes and shutdown
		 */
		notkilled();
		q->slave = up;
		q->busy = 1;	/* fcall in progress: interruptible */
		fs = q->export;
		lock(fs);
		q->next = fs->work;
		fs->work = q;
		unlock(fs);
		unlock(&exq.l);

		up->env->pgrp = q->export->pgrp;
		up->env->egrp = q->export->egrp;
		up->env->fgrp = q->export->fgrp;
		kstrdup(&up->env->user, q->export->user);

		if(exdebug > 1)
			print("exslave %d dispatch %F\n", up->pid, &q->in);

		if(waserror()){
			print("exslave %d err %s\n", up->pid, up->env->errstr);	/* shouldn't happen */
			err = up->env->errstr;
		}else{
			if(q->in.type >= Tmax || !fcalls[q->in.type]){
				snprint(up->genbuf, sizeof(up->genbuf), "unknown message: %F", &q->in);
				err = up->genbuf;
			}else{
				switch(q->in.type){
				case Tread:
					q->out.data = (char*)q->buf + IOHDRSZ;
					break;
				case Tstat:
					q->out.stat = q->buf + MSGHDRSZ + BIT16SZ;	/* leaves it just where we want it */
					nstat = q->bsize;
					if(nstat > STATMAX)
						nstat = STATMAX;
					nstat -= MSGHDRSZ+BIT16SZ;
					q->out.nstat = nstat;
					break;
				}
				err = (*fcalls[q->in.type])(fs, &q->in, &q->out);
			}
			poperror();
		}

		/*
		 * if the fcall completed without error we must reply,
		 * even if a flush is pending (because the underlying server
		 * might have changed state), unless the export has shut down completely.
		 * must also reply to each flush in order, and only after the original reply (if sent).
		 */
		lock(q);
		notkilled();
		q->busy = 0;	/* operation complete */
		if(!q->shut){
			if(q->flush == nil || err == nil){
				unlock(q);
				q->out.type = q->in.type+1;
				q->out.tag = q->in.tag;
				if(err){
					q->out.type = Rerror;
					q->out.ename = err;
				}
				exreply(q, "exslave");
				lock(q);
			}
			while((fq = q->flush) != nil && !q->shut){
				q->flush = fq->next;
				unlock(q);
				exreply(fq, "exslave");
				exfreeq(fq);
				lock(q);
			}
		}
		q->finished = 1;	/* promise not to send any more */
		unlock(q);

		lock(fs);
		for(last = &fs->work; (t = *last) != nil; last = &t->next)
			if(t == q){
				*last = q->next;
				break;
			}
		unlock(fs);

		notkilled();
		exfreeq(q);
	}
}

static void
exreply(Exq *q, char *who)
{
	Export *fs;
	Fcall *r;
	int n;

	fs = q->export;
	r = &q->out;

	n = convS2M(r, q->buf, q->bsize);
	if(n == 0){
		r->type = Rerror;
		if(fs->msize == 0)
			r->ename = "Tversion not seen";
		else
			r->ename = "failed to convert R-message";
		n = convS2M(r, q->buf, q->bsize);
	}

	if(exdebug)
		print("%s %d -> %F\n", who, up->pid, r);

	if(!waserror()){
		devtab[fs->io->type]->write(fs->io, q->buf, n, 0);
		poperror();
	}
}

static int
exiounit(Export *fs, Chan *c)
{
	int iounit;

	iounit = fs->msize-IOHDRSZ;
	if(c->iounit != 0 && c->iounit < fs->msize)
		iounit = c->iounit;
	return iounit;
}

static Qid
Exrmtqid(Chan *c, Uqid *qid)
{
	Qid q;

	q.path = qid->newpath;
	q.vers = c->qid.vers;
	q.type = c->qid.type;
	return q;
}

static Fid*
Exmkfid(Export *fs, ulong fid)
{
	ulong h;
	Fid *f, *nf;

	nf = malloc(sizeof(Fid));
	if(nf == nil)
		return nil;
	lock(&fs->fidlock);
	h = fid % Nfidhash;
	for(f = fs->fid[h]; f != nil; f = f->next){
		if(f->fid == fid){
			unlock(&fs->fidlock);
			free(nf);
			return nil;
		}
	}

	nf->next = fs->fid[h];
	if(nf->next != nil)
		nf->next->last = &nf->next;
	nf->last = &fs->fid[h];
	fs->fid[h] = nf;

	nf->fid = fid;
	nf->ref = 1;
	nf->attached = 1;
	nf->offset = 0;
	nf->chan = nil;
	nf->qid = nil;
	unlock(&fs->fidlock);
	return nf;
}

static Fid*
Exgetfid(Export *fs, ulong fid)
{
	Fid *f;
	ulong h;

	lock(&fs->fidlock);
	h = fid % Nfidhash;
	for(f = fs->fid[h]; f; f = f->next) {
		if(f->fid == fid){
			if(f->attached == 0)
				break;
			f->ref++;
			unlock(&fs->fidlock);
			return f;
		}
	}
	unlock(&fs->fidlock);
	return nil;
}

static void
Exputfid(Export *fs, Fid *f)
{
	Chan *c;

	lock(&fs->fidlock);
	f->ref--;
	if(f->ref == 0 && f->attached == 0){
		c = f->chan;
		f->chan = nil;
		*f->last = f->next;
		if(f->next != nil)
			f->next->last = f->last;
		unlock(&fs->fidlock);
		if(c != nil)
			cclose(c);
		freeuqid(fs, f->qid);
		free(f);
		return;
	}
	unlock(&fs->fidlock);
}

static Chan*
exmount(Chan *c, Mhead **mp, int doname)
{
	Chan *nc;
	Cname *oname;

	nc = nil;
	if((c->flag & COPEN) == 0 && findmount(&nc, mp, c->type, c->dev, c->qid)){
		if(waserror()){
			cclose(nc);
			nexterror();
		}
		nc = cunique(nc);
		poperror();
		if(doname){
			oname = c->name;
			incref(oname);
			cnameclose(nc->name);
			nc->name = oname;
		}
		return nc;
	}
	incref(c);
	return c;
}

static char*
Exversion(Export *fs, Fcall *t, Fcall *r)
{
	char *p;
	static char version[] = VERSION9P;
	int iounit;

	r->msize = t->msize;
	if(r->msize > MAXRPCMAX)
		r->msize = MAXRPCMAX;
	iounit = fs->io->iounit;
	if(iounit != 0 && iounit > 64 && iounit < r->msize)
		r->msize = iounit;
	if(r->msize < 64)
		return "message size too small";
	if((p = strchr(t->version, '.')) != nil)
		*p = 0;
	if(strncmp(t->version, "9P", 2) ==0 && strcmp(version, t->version) <= 0){
		r->version = version;
		fs->msize = r->msize;
	}else
		r->version = "unknown";
	return nil;
}

static char*
Exauth(Export *fs, Fcall *t, Fcall *r)
{
	USED(fs);
	USED(t);
	USED(r);
	return "authentication not required";
}

static char*
Exattach(Export *fs, Fcall *t, Fcall *r)
{
	Fid *f;

	f = Exmkfid(fs, t->fid);
	if(f == nil)
		return Edupfid;
	if(waserror()){
		f->attached = 0;
		Exputfid(fs, f);
		return up->env->errstr;
	}
	f->chan = cclone(fs->root);
	f->qid = uqidalloc(fs, f->chan);
	poperror();
	r->qid = Exrmtqid(f->chan, f->qid);
	Exputfid(fs, f);
	return nil;
}

static char*
Exclunk(Export *fs, Fcall *t, Fcall *r)
{
	Fid *f;

	USED(r);
	f = Exgetfid(fs, t->fid);
	if(f == nil)
		return Enofid;
	f->attached = 0;
	Exputfid(fs, f);
	return nil;
}

static int
safewalk(Chan **cp, char **names, int nnames, int nomount, int *nerror)
{
	int r;

	/* walk can raise error */
	if(waserror())
		return -1;
	r = walk(cp, names, nnames, nomount, nerror);
	poperror();
	return r;
}

static char*
Exwalk(Export *fs, Fcall *t, Fcall *r)
{
	Fid *f, *nf;
	Chan *c;
	char *name;
	Uqid *qid;
	int i;

	f = Exgetfid(fs, t->fid);
	if(f == nil)
		return Enofid;
	if(f->chan->flag & COPEN){
		Exputfid(fs, f);
		return Eopen;
	}

	if(waserror())
		return up->env->errstr;
	c = cclone(f->chan);
	poperror();
	qid = f->qid;
	incref(qid);
	r->nwqid = 0;
	if(t->nwname > 0){
		for(i=0; i<t->nwname; i++){
			name = t->wname[i];
			if(!exisroot(fs, c) || *name != '\0' && strcmp(name, "..") != 0){
				if(safewalk(&c, &name, 1, 0, nil) < 0){
					/* leave the original state on error */
					cclose(c);
					freeuqid(fs, qid);
					Exputfid(fs, f);
					if(i == 0)
						return up->env->errstr;
					return nil;
				}
				freeuqid(fs, qid);
				qid = uqidalloc(fs, c);
			}
			r->wqid[r->nwqid++] = Exrmtqid(c, qid);
		}
	}

	if(t->newfid != t->fid){
		nf = Exmkfid(fs, t->newfid);
		if(nf == nil){
			cclose(c);
			freeuqid(fs, qid);
			Exputfid(fs, f);
			return Edupfid;
		}
		nf->chan = c;
		nf->qid = qid;
		Exputfid(fs, nf);
	}else{
		cclose(f->chan);
		f->chan = c;
		freeuqid(fs, f->qid);
		f->qid = qid;
	}
	Exputfid(fs, f);
	return nil;
}

static char*
Exopen(Export *fs, Fcall *t, Fcall *r)
{
	Fid *f;
	Chan *c;
	Uqid *qid;
	Mhead *m;

	f = Exgetfid(fs, t->fid);
	if(f == nil)
		return Enofid;
	if(f->chan->flag & COPEN){
		Exputfid(fs, f);
		return Emode;
	}
	m = nil;
	c = exmount(f->chan, &m, 1);
	if(waserror()){
		cclose(c);
		Exputfid(fs, f);
		return up->env->errstr;
	}

	/* only save the mount head if it's a multiple element union */
	if(m && m->mount && m->mount->next)
		c->umh = m;
	else
		putmhead(m);

	c = devtab[c->type]->open(c, t->mode);
	if(t->mode & ORCLOSE)
		c->flag |= CRCLOSE;

	qid = uqidalloc(fs, c);
	poperror();
	freeuqid(fs, f->qid);
	cclose(f->chan);
	f->chan = c;
	f->qid = qid;
	f->offset = 0;
	r->qid = Exrmtqid(c, f->qid);
	r->iounit = exiounit(fs, c);
	Exputfid(fs, f);
	return nil;
}

static char*
Excreate(Export *fs, Fcall *t, Fcall *r)
{
	Fid *f;
	volatile struct {Chan *c;} c, dc;
	Cname *oname;
	Uqid *qid;
	Mhead *m;

	f = Exgetfid(fs, t->fid);
	if(f == nil)
		return Enofid;
	if(f->chan->flag & COPEN){
		Exputfid(fs, f);
		return Emode;
	}
	if(waserror()){
		Exputfid(fs, f);
		return up->env->errstr;
	}
	validname(t->name, 0);
	if(t->name[0] == '.' && (t->name[1] == '\0' || t->name[1] == '.' && t->name[2] == '\0'))
		error(Efilename);	/* underlying server should check, but stop it here */

	m = nil;
	c.c = exmount(f->chan, &m, 1);
	if(waserror()){
		cclose(c.c);
		if(m != nil)
			putmhead(m);
		nexterror();
	}
	if(m != nil){
		oname = c.c->name;
		incref(oname);
		if(waserror()){
			cnameclose(oname);
			nexterror();
		}
		dc.c = createdir(c.c, m);
		if(waserror()){
			cclose(dc.c);
			nexterror();
		}
		c.c = cunique(dc.c);
		poperror();
		cnameclose(c.c->name);
		poperror();
		c.c->name = oname;
	}
	devtab[c.c->type]->create(c.c, t->name, t->mode, t->perm);
	c.c->name = addelem(c.c->name, t->name);
	if(t->mode & ORCLOSE)
		c.c->flag |= CRCLOSE;
	qid = uqidalloc(fs, c.c);
	poperror();
	if(m != nil)
		putmhead(m);

	poperror();
	cclose(f->chan);
	f->chan = c.c;
	freeuqid(fs, f->qid);
	f->qid = qid;
	r->qid = Exrmtqid(c.c, f->qid);
	r->iounit = exiounit(fs, c.c);
	Exputfid(fs, f);
	return nil;
}

static char*
Exread(Export *fs, Fcall *t, Fcall *r)
{
	Fid *f;
	Chan *c;
	vlong off;
	int dir, n, seek;

	f = Exgetfid(fs, t->fid);
	if(f == nil)
		return Enofid;

	if(waserror()) {
		Exputfid(fs, f);
		return up->env->errstr;
	}
	c = f->chan;
	if((c->flag & COPEN) == 0)
		error(Emode);
	if(c->mode != OREAD && c->mode != ORDWR)
		error(Eaccess);
	if(t->count < 0 || t->count > fs->msize-IOHDRSZ)
		error(Ecount);
	if(t->offset < 0)
		error(Enegoff);
	dir = c->qid.type & QTDIR;
	if(dir && t->offset != f->offset){
		if(t->offset != 0)
			error(Eseekdir);
		f->offset = 0;
		c->uri = 0;
		c->dri = 0;
	}

	for(;;){
		n = t->count;
		seek = 0;
		off = t->offset;
		if(dir && f->offset != off){
			off = f->offset;
			n = t->offset - off;
			if(n > MAXFDATA)
				n = MAXFDATA;
			seek = 1;
		}
		if(dir && c->umh != nil){
			if(0)
				print("union read %d uri %d dri %d\n", seek, c->uri, c->dri);
			n = unionread(c, r->data, n);
		}
		else{
			c->offset = off;
			n = devtab[c->type]->read(c, r->data, n, off);
			lock(c);
			c->offset += n;
			unlock(c);
		}
		f->offset = off + n;
		if(n == 0 || !seek)
			break;
	}
	r->count = n;

	poperror();
	Exputfid(fs, f);
	return nil;
}

static char*
Exwrite(Export *fs, Fcall *t, Fcall *r)
{
	Fid *f;
	Chan *c;

	f = Exgetfid(fs, t->fid);
	if(f == nil)
		return Enofid;
	if(waserror()){
		Exputfid(fs, f);
		return up->env->errstr;
	}
	c = f->chan;
	if((c->flag & COPEN) == 0)
		error(Emode);
	if(c->mode != OWRITE && c->mode != ORDWR)
		error(Eaccess);
	if(c->qid.type & QTDIR)
		error(Eisdir);
	if(t->count < 0 || t->count > fs->msize-IOHDRSZ)
		error(Ecount);
	if(t->offset < 0)
		error(Enegoff);
	r->count = devtab[c->type]->write(c, t->data, t->count, t->offset);
	poperror();
	Exputfid(fs, f);
	return nil;
}

static char*
Exstat(Export *fs, Fcall *t, Fcall *r)
{
	Fid *f;
	Chan *c;
	int n;

	f = Exgetfid(fs, t->fid);
	if(f == nil)
		return Enofid;
	c = exmount(f->chan, nil, 1);
	if(waserror()){
		cclose(c);
		Exputfid(fs, f);
		return up->env->errstr;
	}
	n = devtab[c->type]->stat(c, r->stat, r->nstat);
	if(n <= BIT16SZ)
		error(Eshortstat);
	r->nstat = n;
	poperror();
	cclose(c);
	Exputfid(fs, f);
	return nil;
}

static char*
Exwstat(Export *fs, Fcall *t, Fcall *r)
{
	Fid *f;
	Chan *c;

	USED(r);
	f = Exgetfid(fs, t->fid);
	if(f == nil)
		return Enofid;
	if(waserror()){
		Exputfid(fs, f);
		return up->env->errstr;
	}
	validstat(t->stat, t->nstat);	/* check name */

	c = exmount(f->chan, nil, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	devtab[c->type]->wstat(c, t->stat, t->nstat);
	poperror();

	cclose(c);
	poperror();
	Exputfid(fs, f);
	return nil;
}

static char*
Exremove(Export *fs, Fcall *t, Fcall *r)
{
	Fid *f;
	Chan *c;

	USED(r);
	f = Exgetfid(fs, t->fid);
	if(f == nil)
		return Enofid;
	if(waserror()){
		f->attached = 0;
		Exputfid(fs, f);
		return up->env->errstr;
	}
	c = exmount(f->chan, nil, 0);
	if(waserror()){
		c->type = 0;	/* see below */
		cclose(c);
		nexterror();
	}
	devtab[c->type]->remove(c);
	poperror();
	poperror();

	/*
	 * chan is already clunked by remove.
	 * however, we need to recover the chan,
	 * and follow sysremove's lead in making it point to root.
	 */
	c->type = 0;

	cclose(c);
	f->attached = 0;
	Exputfid(fs, f);
	return nil;
}

/*
 * unique path generation
 */

static int
uqidhash(vlong path)
{
	ulong p;
	p = (ulong)path;
	return ((p>>16) ^ (p>>8) ^ p) & (Nqidhash-1);
}

static Uqid **
uqidlook(Uqid **tab, Chan *c, vlong path)
{
	Uqid **hp, *q;

	for(hp = &tab[uqidhash(path)]; (q = *hp) != nil; hp = &q->next)
		if(q->type == c->type && q->dev == c->dev && q->oldpath == path)
			break;
	return hp;
}

static int
uqidexists(Uqid **tab, vlong path)
{
	int i;
	Uqid *q;

	for(i=0; i<Nqidhash; i++)
		for(q = tab[i]; q != nil; q = q->next)
			if(q->newpath == path)
				return 1;
	return 0;
}

static Uqid *
uqidalloc(Export *fs, Chan *c)
{
	Uqid **hp, *q;

	qlock(&fs->qidlock);
	hp = uqidlook(fs->qids, c, c->qid.path);
	if((q = *hp) != nil){
		incref(q);
		qunlock(&fs->qidlock);
		return q;
	}
	q = mallocz(sizeof(*q), 1);
	if(q == nil){
		qunlock(&fs->qidlock);
		error(Enomem);
	}
	q->ref = 1;
	q->type = c->type;
	q->dev = c->dev;
	q->oldpath = c->qid.path;
	q->newpath = c->qid.path;
	while(uqidexists(fs->qids, q->newpath)){
		if(++fs->pathgen >= (1<<16))
			fs->pathgen = 1;
		q->newpath = ((vlong)fs->pathgen<<48) | (q->newpath & QIDMASK);
	}
	q->next = nil;
	*hp = q;
	qunlock(&fs->qidlock);
	return q;
}

static void
freeuqid(Export *fs, Uqid *q)
{
	Uqid **hp;

	if(q == nil)
		return;
	qlock(&fs->qidlock);
	if(decref(q) == 0){
		hp = &fs->qids[uqidhash(q->oldpath)];
		for(; *hp != nil; hp = &(*hp)->next)
			if(*hp == q){
				*hp = q->next;
				free(q);
				break;
			}
	}
	qunlock(&fs->qidlock);
}
