/* Debugger for Ian Collier's Z80 emulator for nc100em
 * Copyright (C) 2009 Nilquader of SPRING!
 *
 * debug.h
 */

#ifndef DEBUG_H
#define DEBUG_H

#define fetch(x) (memptr[(unsigned short)(x)>>14][(x)&16383])

#define REG_HL 0
#define REG_IX 1
#define REG_IY 2

typedef struct _z80regs {
	unsigned char *a, *f, *b, *c, *d, *e, *h, *l;
	unsigned char *a1, *f1, *b1, *c1, *d1, *e1, *h1, *l1;
	unsigned char *r, *i, *iff1, *iff2, *im;
	unsigned short *pc, *sp;
	unsigned short *ix, *iy;
} z80regs;

// Prototypendeklaration
void debug(z80regs regs, int *breakflag);
void readstr(char *prompt, char *stringvar, int maxlen);
void dump(unsigned int addr, unsigned int length);
void memwrite(unsigned int addr);
int memload(char *filename, unsigned int addr);
int memsave(char *filename, unsigned int addr, unsigned int length);
void disasm(unsigned int startaddr, unsigned int length);
int parsecmd(char *cmdln, int *cargs);

#endif
