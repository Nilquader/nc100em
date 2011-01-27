/* Debugger for Ian Collier's Z80 emulator for nc100em
 * Copyright (C) 2009 Nilquader of SPRING!
 *
 * debug.h
 */

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

// disassembler tables
char decode_r[][7]   = {"B", "C", "D", "E", "H", "L", "(HL)", "A",
                        "B", "C", "D", "E", "IXH", "IXL", "(IX+d)", "A",
                        "B", "C", "D", "E", "IYH", "IYL", "(IY+d)", "A"};
char decode_rp[][3]  = {"BC", "DE", "HL", "SP", "BC", "DE", "IX", "SP", "BC", "DE", "IY", "SP"};
char decode_rp2[][3] = {"BC", "DE", "HL", "AF", "BC", "DE", "IX", "SP", "BC", "DE", "IY", "SP"};  
char decode_cc[][3]  = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
char decode_alu[][7] = {"ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP "};
char decode_rot[][4] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};
char decode_im[][4]  = {"0", "0/1", "1", "2"}; // interrupt mode
char decode_bli[][5] = {"LDI", "CPI", "INI", "OUTI", "LDD", "CPD", "IND", "OUTD", "LDIR", "CPIR", "INIR", "OTIR", "LDDR", "CPDR", "INDR", "OTDR"};
char decode_af[][5]  = {"RLCA", "RRCA", "RLA" ,"RRA", "DAA", "CPL", "SCF", "CCF"}; 
char decode_var[][9] = {"RET", "EXX", "JP HL", "LD SP,HL"};
char decode_ed7[][7] = {"LD I,A", "LD R,A", "LD A,I", "LD A,R", "RRD", "RLD", "NOP", "NOP"};
char decode_idx[][4] = {"HL", "IX", "IY"};

// Prototypendeklaration
void debug(z80regs regs, int *breakflag);
void readstr(char *prompt, char *stringvar, int maxlen);
void dump(unsigned int addr, unsigned int length);
void memwrite(unsigned int addr);
int memload(char *filename, unsigned int addr);
int memsave(char *filename, unsigned int addr, unsigned int length);
void disasm(unsigned int startaddr, unsigned int length);
int parsecmd(char *cmdln, int *cargs);
