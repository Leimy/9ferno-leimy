
/*
 * functions (possibly) linked in, complete, from libc.
 */
#define nelem(x)	(sizeof(x)/sizeof((x)[0]))
#define offsetof(s, m)	(u32)(&(((s*)0)->m))
#define assert(x)	if(x){}else _assert("x")

/*
 * mem routines
 */
extern	void*	memccpy(void*, void*, int, ulong);
extern	void*	memset(void*, int, ulong);
extern	int	memcmp(void*, void*, ulong);
extern	void*	memmove(void*, void*, ulong);
extern	void*	memchr(void*, int, ulong);

/*
 * string routines
 */
extern	char*	strcat(char*, char*);
extern	char*	strchr(char*, int);
extern	char*	strrchr(char*, int);
extern	int	strcmp(char*, char*);
extern	char*	strcpy(char*, char*);
extern	char*	strecpy(char*, char*, char*);
extern	char*	strncat(char*, char*, long);
extern	char*	strncpy(char*, char*, long);
extern	int	strncmp(char*, char*, long);
extern	long	strlen(char*);
extern	char*	strdup(char*);
extern	char*	strstr(char*, char*);
extern	int	cistrncmp(char*, char*, int);
extern	int	cistrcmp(char*, char*);
extern	char*	cistrstr(char*, char*);
extern	int	atoi(char*);
extern	int	fullrune(char*, int);

enum
{
	UTFmax		= 4,		/* maximum bytes per rune */
	Runesync	= 0x80,		/* cannot represent part of a UTF sequence (<) */
	Runeself	= 0x80,		/* rune and UTF sequences are the same (<) */
	Runeerror	= 0xFFFD,	/* decoding error in UTF */
	Runemax		= 0x10FFFF,	/* 21-bit rune */
	Runemask	= 0x1FFFFF,	/* bits used by runes (see grep) */
};

/*
 * rune routines
 */
extern	int	runetochar(char*, Rune*);
extern	int	chartorune(Rune*, char*);
extern	char*	utfrune(char*, long);
extern	int	utflen(char*);
extern	int	utfnlen(char*, long);
extern	int	runelen(long);

extern	int	abs(int);

/*
 * print routines
 */
typedef struct Fmt	Fmt;
typedef int (*Fmts)(Fmt*);
struct Fmt{
	uchar	runes;			/* output buffer is runes or chars? */
	void	*start;			/* of buffer */
	void	*to;			/* current place in the buffer */
	void	*stop;			/* end of the buffer; overwritten if flush fails */
	int	(*flush)(Fmt *);	/* called when to == stop */
	void	*farg;			/* to make flush a closure */
	int	nfmt;			/* num chars formatted so far */
	va_list	args;			/* args passed to dofmt */
	int	r;			/* % format Rune */
	int	width;
	int	prec;
	ulong	flags;
};
extern	int	print(char*, ...);
extern	char*	seprint(char*, char*, char*, ...);
extern	char*	vseprint(char*, char*, char*, va_list);
extern	char*	smprint(char*, ...);
extern	char*	vsmprint(char*, va_list);
extern	int	snprint(char*, int, char*, ...);
extern	int	vsnprint(char*, int, char*, va_list);
extern	int	sprint(char*, char*, ...);

extern	int	fmtinstall(int, int (*)(Fmt*));
extern	void	quotefmtinstall(void);
extern	int	fmtprint(Fmt*, char*, ...);
extern	int	fmtstrcpy(Fmt*, char*);

#pragma	varargck	argpos	fmtprint	2
#pragma	varargck	argpos	print		1
#pragma	varargck	argpos	seprint		3
#pragma	varargck	argpos	snprint		3
#pragma	varargck	argpos	sprint		2
#pragma	varargck	argpos	vseprint	3
#pragma	varargck	argpos	vsnprint	3

extern	int	fltconv(Fmt*);

/*
 * math
 */
extern int	isNaN(double);
extern int	isInf(double, int);
extern double	floor(double);
extern double	frexp(double, int*);
extern double	pow10(int);

/*
 * one-of-a-kind
 */
extern	char*	cleanname(char*);
extern	ulong	getcallerpc(void*);

extern	long	strtol(char*, char**, int);
extern	ulong	strtoul(char*, char**, int);
extern	s64	strtoll(char*, char**, int);
extern	u64	strtoull(char*, char**, int);
extern	char	etext[];
extern	char	edata[];
extern	char	end[];
extern	int	getfields(char*, char**, int, int, char*);
extern	int	tokenize(char*, char**, int);
extern	int	dec64(uchar*, int, char*, int);
extern	void	qsort(void*, long, long, int (*)(void*, void*));

extern	int	toupper(int);
extern	char*	netmkaddr(char*, char*, char*);
extern	int	myetheraddr(uchar*, char*);
extern	int	parseether(uchar*, char*);

/*
 * network dialling
 */
#define	NETPATHLEN	40

/*
 * Syscall data structures
 */
#define	MORDER	0x0003	/* mask for bits defining order of mounting */
#define	MREPL	0x0000	/* mount replaces object */
#define	MBEFORE	0x0001	/* mount goes before others in union directory */
#define	MAFTER	0x0002	/* mount goes after others in union directory */
#define	MCREATE	0x0004	/* permit creation in mounted directory */
#define	MCACHE	0x0010	/* cache some data */
#define	MMASK	0x0017	/* all bits on */

#define	OREAD	0	/* open for read */
#define	OWRITE	1	/* write */
#define	ORDWR	2	/* read and write */
#define	OEXEC	3	/* execute, == read but check execute permission */
#define	OTRUNC	16	/* or'ed in (except for exec), truncate file first */
#define	OCEXEC	32	/* or'ed in, close on exec */
#define	ORCLOSE	64	/* or'ed in, remove on close */
#define OEXCL   0x1000	/* or'ed in, exclusive create */

#define	NCONT	0	/* continue after note */
#define	NDFLT	1	/* terminate after note */
#define	NSAVE	2	/* clear note but hold state */
#define	NRSTR	3	/* restore saved state */

typedef struct Qid	Qid;
typedef struct Dir	Dir;
typedef struct Waitmsg	Waitmsg;

#define	STATMAX	65535U	/* max length of machine-independent stat structure */
#define	ERRMAX			128	/* max length of error string */
#define	KNAMELEN		28	/* max length of name held in kernel */

/* bits in Qid.type */
#define QTDIR		0x80		/* type bit for directories */
#define QTAPPEND	0x40		/* type bit for append only files */
#define QTEXCL		0x20		/* type bit for exclusive use files */
#define QTMOUNT		0x10		/* type bit for mounted channel */
#define QTAUTH		0x08		/* type bit for authentication file */
#define QTFILE		0x00		/* plain file */

/* bits in Dir.mode */
#define DMDIR		0x80000000	/* mode bit for directories */
#define DMAPPEND	0x40000000	/* mode bit for append only files */
#define DMEXCL		0x20000000	/* mode bit for exclusive use files */
#define DMMOUNT		0x10000000	/* mode bit for mounted channel */
#define DMREAD		0x4		/* mode bit for read permission */
#define DMWRITE		0x2		/* mode bit for write permission */
#define DMEXEC		0x1		/* mode bit for execute permission */

struct Qid
{
	u64	path;
	u32	vers;
	uchar	type;
};

struct Dir {
	/* system-modified data */
	u16	type;	/* server type */
	u32	dev;	/* server subtype */
	/* file data */
	Qid	qid;	/* unique id from server */
	u32	mode;	/* permissions */
	u32	atime;	/* last read time */
	u32	mtime;	/* last write time */
	s64	length;	/* file length: see <u.h> */
	char	*name;	/* last element of path */
	char	*uid;	/* owner name */
	char	*gid;	/* group name */
	char	*muid;	/* last modifier name */
};

struct Waitmsg
{
	int	pid;	/* of loved one */
	u32	time[3];	/* of loved one and descendants */
	char	msg[ERRMAX];	/* actually variable-size in user mode */
};
