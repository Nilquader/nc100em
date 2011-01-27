/* Emulation of the Z80 CPU with hooks into the other parts of nc100em.
 * Copyright (C) 1994 Ian Collier. nc100em changes (C) 1996-1999 Russell Marks.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include "common.h"
#include "fdc.h"
#include "z80.h"
#include "debug.h"

#define parity(a) (partable[a])

unsigned char partable[256]={
      4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
      0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
      0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
      4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
      0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
      4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
      4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
      0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
      0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
      4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
      4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
      0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
      4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4,
      0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
      0, 4, 4, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0, 4, 4, 0,
      4, 0, 0, 4, 0, 4, 4, 0, 0, 4, 4, 0, 4, 0, 0, 4
   };

extern int irq_mask,irq_status,do_nmi;
extern unsigned long tstates,tsmax;
extern int breakpoint; // flag for breaking and invoking debugger

extern unsigned short pc; /* make pc extern for debugging */

void mainloop(void)
{
unsigned char a, f, b, c, d, e, h, l;
unsigned char r, a1, f1, b1, c1, d1, e1, h1, l1, i, iff1, iff2, im;
//unsigned short pc;
unsigned short ix, iy, sp;
unsigned int radjust;
unsigned int ixoriy, new_ixoriy;
unsigned int intsample;
unsigned char op;
unsigned int count=0;
int interrupted=0;
sigset_t mask,oldmask;
int serial_mask=(nc200?4:1);
int parallel_mask=(nc200?1:4);
int fdc_mask=(nc200?32:0);
z80regs regs;

a=f=b=c=d=e=h=l=a1=f1=b1=c1=d1=e1=h1=l1=i=r=iff1=iff2=im=0;
ixoriy=new_ixoriy=0;
ix=iy=sp=pc=0;
tstates=0;
radjust=0;
breakpoint=0;

// Initializing register set for debugger
regs.a = &a;
regs.f = &f;
regs.b = &b;
regs.c = &c;
regs.d = &d;
regs.e = &e;
regs.h = &h;
regs.l = &l;

regs.a1 = &a1;
regs.f1 = &f1;
regs.b1 = &b1;
regs.c1 = &c1;
regs.d1 = &d1;
regs.e1 = &e1;
regs.h1 = &h1;
regs.l1 = &l1;

regs.r = &r;
regs.i = &i;
regs.iff1 = &iff1;
regs.iff2 = &iff2;
regs.im = &im;
regs.pc = &pc;
regs.sp = &sp;
regs.ix = &ix;
regs.iy = &iy;

/* mask of signals to temporarily block to avoid (v. minor) problem */
sigemptyset(&mask);
sigaddset(&mask,SIGALRM);

while(1)
  {
  /* moved here as reqd by `halt' support in z80ops.c */
  if(tstates>tsmax)
    {
    tstates=0;
    
    /* only wait if we're running real-time (i.e. not running slow).
     * This means we run real-time if we can, or with *everything*
     * (including ints) slowing down consistently otherwise.
     * This should avoid overloading a slow emulated NC with many ints,
     * but at the cost of (e.g.) sluggish keyboard response.
     *
     * The procmask stuff is to avoid a race condition (not actually
     * a big deal, would just rarely lose an interrupt, but FWIW...).
     */
    sigprocmask(SIG_BLOCK,&mask,&oldmask);
    if(!signal_int_flag)
      while(!signal_int_flag)
        sigsuspend(&oldmask);
    sigprocmask(SIG_UNBLOCK,&mask,NULL);
    
    signal_int_flag=0;
    interrupted=1;
    }
  
  ixoriy=new_ixoriy;
  new_ixoriy=0;
  intsample=1;
  
  debug(regs, &breakpoint);
  
  
  op=fetch(pc);
  pc++;
  radjust++;
  switch(op)
    {
#include "z80ops.c"
    }
  
  /* check for printer output ACK IRQ. For some reason the ROM
   * OS seems to think it's a brilliant idea to a) depend on this despite
   * only using it to set a flag, such that using ACK on port A0 would
   * have made more sense, and b) absolutely depend on this being *the*
   * next interrupt to happen after a byte has been sent. If a non-ACK
   * interrupt happens in the meantime, it sends it again, on the assumption
   * that the printer didn't receive it. Great. So in order for the
   * printer support to actually *work*, this therefore has to
   * take priority over all other interrupts.
   *
   * Fortunately, the irq_mask test being first will stop this crock
   * slowing down more clueful operating systems like ZCN more than
   * absolutely necessary. ;-)
   *
   * Here irq_status has the relevant bit cleared by common.c,
   * so we just test for one already having started.
   */
  if((irq_mask&parallel_mask) && iff1 && !(irq_status&parallel_mask))
    if(intsample)	/* no test for interrupted, this is priority... */
      {			/* ...because of the stupidity of the ROM OS */
      /* irq_status dealt with in common.c's put_printer_byte() */
      goto kludge;
      }
  
  count++;
  if(!(count&0x7ff) && iff1)
    {
    /* check for serial input.
     * The `if' means `if not masked, and not doing one already, and
     * serial input pending'.
     */
    if((irq_mask&serial_mask) && (irq_status&serial_mask) &&
       serial_input_pending())
      {
      if(intsample && !interrupted)
        {
        /* serial input int */
        irq_status&=~serial_mask;
        goto kludge;
        }
      else
        count--;	/* try again next instr */
      }
      /* Check for FDC Interrupt (EXPERIMENTAL CODE) */
	  if(1 && (irq_status&fdc_mask) && fdc_interrupt(0)) {
		if(intsample && !interrupted) {
	  		/* fdc int */
	  		irq_status&=~fdc_mask;
	  		fdc_interrupt(1);
	  		printf("FDC Interrupt!\n");
	  		goto kludge;
	  	    }
	  	  else
	  	    count--;
      }
      if ((!(irq_mask&fdc_mask)) && (fdc_interrupt(0))) { /* EXPERIMENTAL CODE - CLEAR MASKED IRQ*/
      	// fdc_interrupt(1); // If FDC IRQ is masked, clear possibe IRQ request from FDC 
      	// printf("MASKED FDC IRQ CLEARED\n");
      }
    }
    

  if(interrupted)
    {
    if(interrupted==1)
      do_interrupt();	/* does the screen update & keyboard reading */
    
    if(!intsample)
      {
      /* this makes sure we don't interrupt a dd/fd/etc. prefix op.
       * we make interrupted non-zero but bigger than one so that
       * we don't redraw screen etc. next time but *do* do int if
       * enabled.
       */
      interrupted=2;
      continue;
      }
    
    interrupted=0;
    if(do_nmi)
      {
      do_nmi=0;
      if(fetch(pc)==0x76)pc++;
      iff2=iff1;
      iff1=0;
      tstates+=7; /* perhaps */
      push2(pc);
      pc=0x66;
      continue;
      }
    
    if(!(iff1 && (irq_mask&8) && (irq_status&8)))
      interrupted=2;	/* keep trying to interrupt */
    else
      {
      irq_status&=0xf7;
      kludge:
      if(fetch(pc)==0x76)pc++;
      iff1=iff2=0;
      tstates+=5; /* accompanied by an input from the data bus */
      switch(im){
        case 0: /* IM 0 */
        case 1: /* undocumented */
        case 2: /* IM 1 */
          /* there is little to distinguish between these cases */
          tstates+=7; /* perhaps */
          push2(pc);
          pc=0x38;
          break;
        case 3: /* IM 2 */
          tstates+=13; /* perhaps */
          {
          int addr=fetch2((i<<8)|0xff);
          push2(pc);
          pc=addr;
          }
        }
      }
    }
  }
}
