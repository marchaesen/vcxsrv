/*
 *  math.c  -  mathematics functions for a hand calculator under X
 *
 *  Author:    John H. Bradley, University of Pennsylvania
 *                (bradley@cis.upenn.edu)
 *                     March, 1987
 *
 *  RPN mode added and port to X11 by Mark Rosenstein, MIT Project Athena
 *
 *  Modified to be a client of the Xt toolkit and the Athena widget set by
 *  Donna Converse, MIT X Consortium.  This is all that remains of the
 *  original calculator, and it still needs to be rewritten.  The HP
 *  functionality should be separated from the TI functionality.
 *  Beware the HP functions: there are still errors here.
 *
 *  Geoffrey Coram fixed most of the HP mode bugs.
 */

#include "xcalc.h"
#include <unistd.h>

#ifndef M_PI        /* sometimes defined in math.h */
#define M_PI        3.14159265358979323846
#endif

#ifndef M_E         /* sometimes defined in math.h */
#define M_E           2.7182818284590452354
#endif

#define MAXDISP     11
#define DEG 0		/* DRG mode.  used for trig calculations */
#define RAD 1
#define GRAD 2

#define True	1
#define False   0

#ifndef IEEE
jmp_buf env;
#endif


/* This section is all of the state machine that implements the calculator
 * functions.  Much of it is shared between the infix and rpn modes.
 */

static int flagINV, flagPAREN, flagM, drgmode, numbase;	/* display flags */

static double drg2rad=M_PI/180.0;  /* Conversion factors for trig funcs */
static double rad2drg=180.0/M_PI;
static int entered=1;  /* true if display contains a valid number.
                          if==2, then use 'dnum', rather than the string
                          stored in the display.  (for accuracy)
                          if==3, then error occurred, only CLR & AC work */
/* entered seems to be overloaded - dmc */
static int lift_enabled = 0;	/* for rpn mode only */

static int CLR    =0;  /* CLR clears display.  if 1, clears acc, also */
static int Dpoint=0;  /* to prevent using decimal pt twice in a # */
static int clrdisp=1;  /* if true clears display before entering # */
static int lastop =kCLR;
static int memop  =kCLR;
static int exponent=0;
static double acc =0.0;
static double dnum=0.0;
#define XCALC_MEMORY 10
static double mem[XCALC_MEMORY] = { 0.0 };

static void   DrawDisplay(void);
static void   PushOp(int op);
static int    PopOp(void);
static int    isopempty(void);
#ifdef DEBUG
static void   showstack(char *string);
#endif
static void   PushNum(double num);
static double PopNum(void);
static void   RollNum(int dir);
static void   ClearStacks(void);
static int    priority(int op);

#ifndef HAVE_STRLCPY
/* Close enough for the short strings copied in xcalc */
static __inline size_t
strlcpy(char *dst, const char *src, size_t size)
{
    strncpy(dst, src, size);
    dst[size - 1] = '\0';
    return strlen(src);
}
#endif

/*
 * The following is to deal with the unfortunate assumption that if errno
 * is non-zero then an error has occurred.  On some systems (e.g. Ultrix),
 * sscanf will call lower level routines that will set errno.
 */
static void
parse_double(double *dp)
{
    unsigned long n = 0;
    int olderrno = errno;

    switch (numbase) {
    case 8:
        (void)sscanf(dispstr, "%lo", &n);
        *dp = (double)n;
    break;
    case 16:
        (void)sscanf(dispstr, "%lX", &n);
        *dp = (double)n;
    break;
    default:
        (void)sscanf(dispstr, "%lf", dp);
    }

    errno = olderrno;
    return;
}

/**
 * Format the given double according to the
 * selected number base.
 */
static void
format_double(double n)
{
    switch (numbase) {
    case 8:
        snprintf(dispstr, sizeof(dispstr), "%lo", (long)n);
    break;
    case 16:
        snprintf(dispstr, sizeof(dispstr), "%lX", (long)n);
    break;
    default:
        snprintf(dispstr, sizeof(dispstr), "%.8g", n);
    }
}

/*********************************/
int pre_op(int keynum)
{
    if (keynum==-1) return(0);

    errno = 0;			/* for non-IEEE machines */

    if ( (entered==3) && !(keynum==kCLR || keynum==kOFF)) {
      if (rpn) {
	clrdisp++;
      } else {
        ringbell();
        return(1);	/* the intent was probably not to do the operation */
      }
    }

    if (keynum != kCLR) CLR=0;
    return(0);
}

#ifndef IEEE

/* cannot assign result of setjmp under ANSI C, use global instead */
static volatile int SignalKind;

void fail_op(void)
{
    if (SignalKind == SIGFPE)
	strlcpy(dispstr, "math error", sizeof(dispstr));
    else if (SignalKind == SIGILL)
	strlcpy(dispstr, "illegal operand", sizeof(dispstr));

    entered=3;
    DrawDisplay();
    return;
}

/*ARGSUSED*/
void fperr(int sig)
{
#if defined(SYSV) || defined(SVR4) || defined(linux)
    signal(SIGFPE, fperr);
#endif
    SignalKind = sig;
    longjmp(env,1);
}

/* for VAX BSD4.3 */
/*ARGSUSED*/
void illerr(int sig)
{
    /* not reset when caught? */
    signal(SIGILL, illerr);

    SignalKind = sig;
    longjmp(env,1);
}

#endif	/* not IEEE */


void post_op(void)
{
#ifdef DEBUG
    showstack("\0");
#endif
#ifndef IEEE
    if (errno) {
        strlcpy(dispstr, "error", sizeof(dispstr));
        DrawDisplay();
        entered=3;
        errno=0;
        }
#endif
}

/*-------------------------------------------------------------------------*/
static void
DrawDisplay(void)
{
    if (strlen(dispstr) >= MAXDISP) { /* strip out some decimal digits */
        char *estr = index(dispstr,'e');  /* search for exponent part */
        if (!estr) dispstr[12]='\0';      /* no exp, just trunc. */
        else {
            char tmp[32];
            if (strlen(estr) <= 4)        /* leftmost 8 chars plus exponent */
                snprintf(tmp, sizeof(tmp), "%.8s%s", dispstr, estr);
            else                          /* leftmost 7 chars plus exponent */
                snprintf(tmp, sizeof(tmp), "%.7s%s", dispstr, estr);
            strlcpy(dispstr, tmp, sizeof(dispstr));
        }
    }
    draw(dispstr);
    setflag(XCalc_MEMORY, (flagM));
    setflag(XCalc_INVERSE, (flagINV));
    setflag(XCalc_DEGREE, (drgmode==DEG));
    setflag(XCalc_RADIAN, (drgmode==RAD));
    setflag(XCalc_GRADAM, (drgmode==GRAD));
    setflag(XCalc_PAREN, (flagPAREN));
    setflag(XCalc_HEX, (numbase==16));
    setflag(XCalc_DEC, (numbase==10));
    setflag(XCalc_OCT, (numbase==8));
}

/*-------------------------------------------------------------------------*/
void
change_base(void)
{
	parse_double(&dnum);

    if (dnum >= 0) {
        switch (numbase) {
        case 8:  numbase = 10;  break;
        case 10: numbase = 16;  break;
        case 16: numbase = 8;   break;
        }

        format_double(dnum);
    } else strlcpy(dispstr, "error", sizeof(dispstr));

    DrawDisplay();
}

/*-------------------------------------------------------------------------*/
void
numeric(int keynum)
{
  char st[2];

  flagINV=0;

  if (rpn && (memop == kSTO || memop == kRCL || memop == kSUM)) {
      int cell = 0;

      switch (keynum) {
	case kONE:	cell = 1; break;
	case kTWO:	cell = 2; break;
	case kTHREE:	cell = 3; break;
	case kFOUR:	cell = 4; break;
	case kFIVE:	cell = 5; break;
	case kSIX:	cell = 6; break;
	case kSEVEN:	cell = 7; break;
	case kEIGHT:	cell = 8; break;
	case kNINE:	cell = 9; break;
	case kZERO:	cell = 0; break;
      }
      switch (memop) {
      case kSTO:
	mem[cell] = dnum;
	lift_enabled = 1;
	entered = 2;
	clrdisp++;
	break;
      case kRCL:
	PushNum(dnum);
	dnum = mem[cell];
	format_double(dnum);
	lift_enabled = 1;
        entered = 1;
	clrdisp++;
	break;
      case kSUM:
	mem[cell] += dnum;
	lift_enabled = 1;
	entered = 2;
	clrdisp++;
	break;
      }
      memop = kCLR;
      DrawDisplay();
      return;
  }

  if (clrdisp) {
    dispstr[0]='\0';
    exponent=Dpoint=0;
/*    if (rpn && entered==2)
      PushNum(dnum);
 */
    if (rpn & lift_enabled)
	PushNum(dnum);
  }
  if ((int) strlen(dispstr) >= MAXDISP)
    return;

  st[0] = '\0';
  switch (keynum){
      case kZERO:	st[0] = '0'; break;
      case kONE:	st[0] = '1'; break;
      case kTWO:	st[0] = '2'; break;
      case kTHREE:	st[0] = '3'; break;
      case kFOUR:	st[0] = '4'; break;
      case kFIVE:	st[0] = '5'; break;
      case kSIX:	st[0] = '6'; break;
      case kSEVEN:	st[0] = '7'; break;
      case kEIGHT:	if (numbase > 8)  st[0] = '8'; break;
      case kNINE:	if (numbase > 8)  st[0] = '9'; break;
      case kxA: 	if (numbase > 10) st[0] = 'A'; break;
      case kxB: 	if (numbase > 10) st[0] = 'B'; break;
      case kxC: 	if (numbase > 10) st[0] = 'C'; break;
      case kxD: 	if (numbase > 10) st[0] = 'D'; break;
      case kxE: 	if (numbase > 10) st[0] = 'E'; break;
      case kxF: 	if (numbase > 10) st[0] = 'F'; break;
    }

    if (st[0] == '\0')
        return;
    st[1] = '\0';
    strcat(dispstr,st);

  DrawDisplay();
  if (clrdisp && keynum != kZERO)
    clrdisp=0; /*no leading 0s*/
  memop = keynum;
  entered=1;
  lift_enabled = 0;
}

void
bkspf(void)
{

  lift_enabled = 0;

  if (! flagINV)
  {
      if (entered!=1) {
	  clearf();
	  return;
      }
      if (clrdisp)
	  return;
      if ((int) strlen(dispstr) > 0) {
#ifndef X_LOCALE
          const char *dp = localeconv()->decimal_point;
          size_t dp_len = strlen(dp);
          size_t ds_len = strlen(dispstr);
          if (ds_len >= dp_len && strcmp(dispstr + ds_len - dp_len, dp) == 0)
             Dpoint=0;
#else
	  if (dispstr[strlen(dispstr)-1] == '.')
             Dpoint=0;
#endif
	  dispstr[strlen(dispstr)-1] = 0;
      }
      if (strlen(dispstr) == 0) {
	  strcat(dispstr, "0");
	  clrdisp++;
      }
  }
  else
  {
      strlcpy(dispstr, "0", sizeof(dispstr));
      dnum = 0.0;
      clrdisp++;
      flagINV = 0;
  }
  DrawDisplay();
}

void
decf(void)
{
  flagINV=0;
  if (clrdisp) {
      if (rpn && lift_enabled)
	PushNum(dnum);
      strlcpy(dispstr, "0", sizeof(dispstr));
  }
  if (!Dpoint) {
#ifndef X_LOCALE
    strcat(dispstr, localeconv()->decimal_point);
#else
    strcat(dispstr, ".");
#endif
    DrawDisplay();
    Dpoint++;
  }
  clrdisp=0;
  entered=1;
}

void
eef(void)
{
  flagINV=0;
  if (clrdisp) {
      if (rpn && lift_enabled)
	PushNum(dnum);
      strlcpy(dispstr, rpn ? "1" : "0", sizeof(dispstr));
  }
  if (!exponent) {
    strcat(dispstr,"E+");
    DrawDisplay();
    exponent=strlen(dispstr)-1;  /* where the '-' goes */
  }
  clrdisp=0;
  entered=1;
}

void
clearf(void)
{
  flagINV=0;
  if (CLR && !rpn) { /* clear all */
    ClearStacks();
    flagPAREN=0;
  }
  CLR++;
  exponent=Dpoint=0;
  clrdisp=1;
  entered=1;
  strlcpy(dispstr, "0", sizeof(dispstr));
  DrawDisplay();
}

void
negf(void)
{
  flagINV=0;
  if (exponent) {       /* neg the exponent */
    if (dispstr[exponent]=='-')
      dispstr[exponent]='+';
    else
      dispstr[exponent]='-';
    DrawDisplay();
    return;
  }

  if (strcmp("0",dispstr)==0)
    return;			/* don't neg a zero */
  if (dispstr[0]=='-')	 	/* already neg-ed */
    strcpy(dispstr,dispstr+1);  /* move str left once */
  else {			/* not neg-ed.  add a '-' */
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "-%s", dispstr);
    strlcpy(dispstr, tmp, sizeof(dispstr));
  }
  if (entered==2)
    dnum = -1.0 * dnum;
  DrawDisplay();
}

/* Two operand functions for infix calc */
void
twoop(int keynum)
{
  if (flagINV) {
    flagINV=0;
    DrawDisplay();
  }

  if (!entered) {		/* something like "5+*" */
    if (!isopempty())
      (void) PopOp();			/* replace the prev op */
    PushOp(keynum);		/* with the new one */
    return;
  }

  if (entered==1)
    parse_double(&dnum);

  clrdisp=CLR=1;
  entered=Dpoint=exponent=0;

  if (!isopempty()) {  /* there was a previous op */
    lastop=PopOp();   /* get it */

    if (lastop==kLPAR) {  /* put it back */
      PushOp(kLPAR);
      PushOp(keynum);
      PushNum(dnum);
      return;
    }

    /* now, if the current op (keynum) is of
       higher priority than the lastop, the current
       op and number are just pushed on top
       Priorities:  (Y^X) > *,/ > +,- > >>,<< > & > ^ > ~ */

    if (priority(keynum) > priority(lastop)) {
      PushNum(dnum);
      PushOp(lastop);
      PushOp(keynum);
    } else {  /* execute lastop on lastnum and dnum, push
	       result and current op on stack */
      acc=PopNum();
      switch (lastop) { /* perform the operation */
      case kADD: acc += dnum;  break;
      case kSUB: acc -= dnum;  break;
      case kMUL: acc *= dnum;  break;
      case kDIV: acc /= dnum;  break;
      case kPOW: acc =  pow(acc,dnum);  break;
      case kMOD: acc = (long)acc %  (long)dnum;  break;
      case kAND: acc = (long)acc &  (long)dnum;  break;
      case kOR:  acc = (long)acc |  (long)dnum;  break;
      case kXOR: acc = (long)acc ^  (long)dnum;  break;
      case kSHL: acc = (long)acc << (long)dnum;  break;
      case kSHR: acc = (long)acc >> (long)dnum;  break;
      }

      PushNum(acc);
      PushOp(keynum);
      format_double(acc);
      DrawDisplay();
      dnum=acc;
    }
  }
  else { /* op stack is empty, push op and num */
    PushOp(keynum);
    PushNum(dnum);
  }
}

/* Two operand functions for rpn calc */
void
twof(int keynum)
{
  if (flagINV) {
    flagINV=0;
    DrawDisplay();
  }
  if (!entered)
    return;
  if (entered==1)
    parse_double(&dnum);
  acc = PopNum();
  switch(keynum) {
  case kADD: acc += dnum;  break;
  case kSUB: acc -= dnum;  break;
  case kMUL: acc *= dnum;  break;
  case kDIV: acc /= dnum;  break;
  case kPOW: acc =  pow(acc,dnum);  break;
  case kXXY: PushNum(dnum);  break;
  case kMOD: acc = (long)acc %  (long)dnum;  break;
  case kAND: acc = (long)acc &  (long)dnum;  break;
  case kOR:  acc = (long)acc |  (long)dnum;  break;
  case kXOR: acc = (long)acc ^  (long)dnum;  break;
  case kSHL: acc = (long)acc << (long)dnum;  break;
  case kSHR: acc = (long)acc >> (long)dnum;  break;
  }

  format_double(acc);
  DrawDisplay();
  clrdisp++;
  Dpoint = exponent = 0;
  entered = 2;
  lift_enabled = 1;
  dnum = acc;
}

void
entrf(void)
{
  flagINV=0;
  if (!entered)
    return;

  clrdisp=CLR=1;
  Dpoint=exponent=0;

  if (entered==1)
    parse_double(&dnum);
  entered=2;
  memop = kENTR;
  PushNum(dnum);
  lift_enabled = 0;
}

void
equf(void)
{
  flagINV=0;
  if (!entered)
    return;

  clrdisp=CLR=1;
  Dpoint=exponent=0;

  if (entered==1)
    parse_double(&dnum);
  entered=2;

  PushNum(dnum);

  while (!isopempty()) {  /* do all pending ops */
    dnum=PopNum();
    acc=PopNum();
    lastop=PopOp();
    switch (lastop) {
    case kADD:  acc += dnum;
		break;
    case kSUB:  acc -= dnum;
		break;
    case kMUL:  acc *= dnum;
		break;
    case kDIV:  acc /= dnum;
		break;
    case kPOW:  acc = pow(acc,dnum);
		break;
    case kLPAR:	flagPAREN--;
		PushNum(acc);
		break;
    case kMOD:  acc = (long)acc % (long)dnum;
		break;
    case kAND:  acc = (long)acc & (long)dnum;
		break;
    case kOR:   acc = (long)acc | (long)dnum;
		break;
    case kXOR:  acc = (long)acc ^ (long)dnum;
		break;
    case kSHL:  acc = (long)acc << (long)dnum;
		break;
    case kSHR:  acc = (long)acc >> (long)dnum;
		break;
    }
    dnum=acc;
    PushNum(dnum);
  }

  format_double(dnum);
  DrawDisplay();
}

void
lparf(void)
{
  flagINV=0;
  PushOp(kLPAR);
  flagPAREN++;
  DrawDisplay();
}

void
rollf(void)
{
  if (!entered)
    return;
  if (entered==1)
    parse_double(&dnum);
  entered = 2;
  lift_enabled = 1;
  RollNum(flagINV);
  flagINV=0;
  clrdisp++;
  format_double(dnum);
  DrawDisplay();
}

void
rparf(void)
{
  flagINV=0;
  if (!entered)
    return;

  if (!flagPAREN)
    return;

  clrdisp++;
  Dpoint=exponent=0;

  if (entered==1)
    parse_double(&dnum);
  entered=2;

  PushNum(dnum);
  while (!isopempty() && (lastop=PopOp())!=kLPAR) {
    /* do all pending ops, back to left paren */
    dnum=PopNum();
    acc=PopNum();
    switch (lastop) {
    case kADD:  acc += dnum;
		break;
    case kSUB:  acc -= dnum;
		break;
    case kMUL:  acc *= dnum;
		break;
    case kDIV:  acc /= dnum;
		break;
    case kPOW:  acc = pow(acc,dnum);
		break;
    case kMOD:  acc = (long)acc % (long)dnum;
		break;
    case kAND:  acc = (long)acc & (long)dnum;
		break;
    case kOR:   acc = (long)acc | (long)dnum;
		break;
    case kXOR:  acc = (long)acc ^ (long)dnum;
		break;
    case kSHL:  acc = (long)acc << (long)dnum;
		break;
    case kSHR:  acc = (long)acc >> (long)dnum;
		break;
    }
    dnum=acc;
    PushNum(dnum);
  }
  (void) PopNum();
  flagPAREN--;
  entered=2;
  format_double(dnum);
  DrawDisplay();
}

void
drgf(void)
{
  if (flagINV) {
    if (entered==1)
      parse_double(&dnum);
    switch (drgmode) {
    case DEG:  dnum=dnum*M_PI/180.0;    break;
    case RAD:  dnum=dnum*200.0/M_PI;    break;
    case GRAD: dnum=dnum*90.0/100.0;  break;
    }
    entered=2;
    clrdisp=1;
    flagINV=0;
    format_double(dnum);
  }

  flagINV=0;
  drgmode = (drgmode + 1) % 3;
  switch (drgmode) {
  case DEG:  drg2rad=M_PI / 180.0;
	     rad2drg=180.0 / M_PI;
	     break;
  case RAD:  drg2rad=1.0;
	     rad2drg=1.0;
	     break;
  case GRAD: drg2rad=M_PI / 200.0;
	     rad2drg=200.0 / M_PI;
	     break;
  }
  DrawDisplay();
}

void
invf(void)
{
  flagINV = ~flagINV;
  DrawDisplay();
}

void
memf(int keynum)
{
    memop = keynum;
    if (entered==1)
      parse_double(&dnum);
    entered = 2;
    clrdisp++;
    lift_enabled = 0;
}

void
oneop(int keynum)
{
  int i,j;
  double dtmp;

  if (entered==1)
    parse_double(&dnum);
  entered = 2;

  switch (keynum) {  /* do the actual math fn. */
  case kE:     if (rpn && memop != kENTR) PushNum(dnum); dnum=M_E;  break;
  case kPI:    if (rpn && memop != kENTR) PushNum(dnum); dnum=M_PI;  break;
  case kRECIP: dnum=1.0/dnum;  break;
  case kSQR:   flagINV = !flagINV; /* fall through */
  case kSQRT:  if (flagINV) dnum=dnum*dnum;
	       else dnum=sqrt(dnum);
	       break;
  case k10X:   flagINV = !flagINV; /* fall through */
  case kLOG:   if (flagINV) dnum=pow(10.0,dnum);
  	       else dnum=log10(dnum);
	       break;
  case kEXP:   flagINV = !flagINV; /* fall through */
  case kLN:    if (flagINV) dnum=exp(dnum);
	       else dnum=log(dnum);
	       break;
  case kSIN:   if (flagINV) dnum=asin(dnum)*rad2drg;
	       else dnum=sin(dnum*drg2rad);
	       break;
  case kCOS:   if (flagINV) dnum=acos(dnum)*rad2drg;
	       else dnum=cos(dnum*drg2rad);
	       break;
  case kTAN:   if (flagINV) dnum=atan(dnum)*rad2drg;
	       else dnum=tan(dnum*drg2rad);
	       break;
  case kSTO:   mem[0]=dnum;  flagM=!(mem[0]==0.0);  break;
  case kRCL:   if (rpn && lift_enabled) PushNum(dnum);
               dnum=mem[0];  flagM=!(mem[0]==0.0);  break;
  case kSUM:   mem[0]+=dnum; flagM=!(mem[0]==0.0);  break;
  case kEXC:   dtmp=dnum; dnum=mem[0];  mem[0]=dtmp;
	       flagM=!(mem[0]==0.0);  break;
  case kFACT:  if (floor(dnum)!=dnum || dnum<0.0 || dnum>500.0) {
                 strlcpy(dispstr, "error", sizeof(dispstr));
		 entered=3;
		 break;
	       }
	       dtmp = floor(dnum); i = dtmp;
	       for (j=1,dnum=1.0; j<=i; j++)
		 dnum*=(float) j;
	       break;
  case kNOT:   dnum = ~(long)dnum;  break;
  case kTRUNC: dnum = trunc(dnum);  break;
  }

  if (entered==3) {  /* error */
    DrawDisplay();
    return;
  }

  memop = keynum;
  entered=2;
  clrdisp=1;
  flagINV=0;
  lift_enabled = 1;
  format_double(dnum);
  DrawDisplay();
}

void
offf(void)
{
  /* full reset */
  ResetCalc();
  entered=clrdisp=1;
  lift_enabled = 0;
  dnum=mem[0]=0.0;
  if (rpn)
      for (int i=1; i < XCALC_MEMORY; i++)
	  mem[i]=0.0;
  exponent=Dpoint=0;
  DrawDisplay();
}


#define STACKMAX 32
static int opstack[STACKMAX];
static int opsp;
static double numstack[STACKMAX];
static int numsp;


/*******/
static void
PushOp(int op)
/*******/
{
  if (opsp==STACKMAX) {
      strlcpy(dispstr, "stack error", sizeof(dispstr));
      entered=3;
  } else
      opstack[opsp++]=op;
}

/*******/
static int
PopOp(void)
/*******/
{
  if (opsp==0) {
      strlcpy(dispstr, "stack error", sizeof(dispstr));
      entered=3;
      return(kNOP);
  } else
    return(opstack[--opsp]);
}

/*******/
static int
isopempty(void)
/*******/
{
  return( opsp ? 0 : 1 );
}

#ifdef DEBUG
static void
showstack(char *string)
{
    fprintf(stderr, "%s: %lf %lf %lf\n", string, numstack[0], numstack[1],
	    numstack[2]);
}
#endif

/*******/
static void
PushNum(double num)
/*******/
{
  if (rpn) {
      numstack[2] = numstack[1];
      numstack[1] = numstack[0];
      numstack[0] = num;
      return;
  }
  if (numsp==STACKMAX) {
      strlcpy(dispstr, "stack error", sizeof(dispstr));
      entered=3;
  } else
    numstack[numsp++]=num;
}

/*******/
static double
PopNum(void)
/*******/
{
    if (rpn) {
	double tmp = numstack[0];
	numstack[0] = numstack[1];
	numstack[1] = numstack[2];
	return(tmp);
    }
    if (numsp==0) {
	strlcpy(dispstr, "stack error", sizeof(dispstr));
	entered=3;
	return 0.0;
    } else
      return(numstack[--numsp]);
}

/*******/
static void
RollNum(int dir)
/*******/
{
    double tmp;

    if (dir) {				/* roll up */
	tmp         = dnum;
	dnum        = numstack[2];
	numstack[2] = numstack[1];
	numstack[1] = numstack[0];
	numstack[0] = tmp;
    } else {				/* roll down */
	tmp         = dnum;
	dnum        = numstack[0];
	numstack[0] = numstack[1];
	numstack[1] = numstack[2];
	numstack[2] = tmp;
    }
}


/*******/
static void
ClearStacks(void)
/*******/
{
    if (rpn)
      numstack[0] = numstack[1] = numstack[2] = 0.;
    opsp=numsp=0;
}


/*******/
static int
priority(int op)
/*******/
{
    switch (op) {
        case kPOW: return(6);
        case kMUL:
        case kDIV:
        case kMOD: return(5);
        case kADD:
        case kSUB: return(4);
        case kSHL:
        case kSHR: return(3);
        case kAND: return(2);
        case kXOR: return(1);
        case kOR:  return(0);
    }
    return 0;
}


/********/
void
ResetCalc(void)
/********/
{
    flagM=flagINV=flagPAREN=0;  drgmode=DEG;
    numbase=(!numbase ? 10 : numbase);
    setflag(XCalc_MEMORY, False);
    setflag(XCalc_INVERSE, False);
    setflag(XCalc_PAREN, False);
    setflag(XCalc_RADIAN, False);
    setflag(XCalc_GRADAM, False);
    setflag(XCalc_DEGREE, True);
    setflag(XCalc_HEX, False);
    setflag(XCalc_DEC, True);
    setflag(XCalc_OCT, False);
    strlcpy(dispstr, "0", sizeof(dispstr));
    draw(dispstr);
    ClearStacks();
    drg2rad=M_PI/180.0;
    rad2drg=180.0/M_PI;
}
