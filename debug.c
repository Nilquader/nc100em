/* Debugger for Ian Collier's Z80 emulator for nc100em
 * Copyright (C) 2009 Nilquader of SPRING!
 *
 * debug.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "common.h"
//#include "z80.h"

#define MAXPARAM 4
#define WRITEOP(s) strcpy(decodeptr,s); decodeptr += strlen(s);  

void debug(z80regs regs, int *breakflag) {
	/* local variables */
	char cmdln[80]; // complete debug cmdline
	char filename[200]; // file name
	int cargs[MAXPARAM];
	int quit = 0;
	static int breakpoint = -1;
	
	if ((*regs.pc == breakpoint) && (breakpoint != -1)) *breakflag = 1; // breakpoint reached
	
	if(*breakflag) { // stop execution and debug
		do {
			// print status
			printf("Z80 Debugger\n");
			printf("PC=&%04X IR=&%02X%02X AF =&%02X%02X BC =&%02X%02X DE =&%02X%02X HL =&%02X%02X IFF1=&%02X IFF2=&%02X\n",
				*regs.pc, *regs.i, *regs.r, *regs.a, *regs.f, *regs.b, *regs.c, *regs.d, *regs.e, *regs.h, *regs.l, *regs.iff1, *regs.iff2);
			printf("IX=&%04X IY=&%04X AF'=&%02X%02X BC'=&%02X%02X DE'=&%02X%02X HL'=&%02X%02X IM=&%02X SP=&%04X\n",
				*regs.ix, *regs.iy, *regs.a1, *regs.f1, *regs.b1, *regs.c1, *regs.d1, *regs.e1, *regs.h1, *regs.l1, *regs.im, *regs.sp);
			printf("[r]un [s]tep [h]exdump [i]nput [d]isasm. [b]reakpoint [l]load [w]rite [q]uit\n\n");
		
			// show debugger command line
			readstr("NC>",cmdln,80);
			parsecmd(cmdln,cargs);
			switch(cmdln[0]) {
				case 'r':
					quit=1;
					*breakflag = 0;
					break;
				case 's':
					quit=1;
					*breakflag = 1;
					break;
				case 'h': // DUMP command
					if(cargs[0] == -1) cargs[0] = *regs.pc; // default start: value of PC
					if(cargs[1] == -1) cargs[1] = 0x0100; // default length: 0x0100 bytes
					dump(cargs[0],cargs[1]);
					break;
				case 'i': // Input command
					if(cargs[0] == -1) cargs[0] = *regs.pc; // default start: value of PC
					memwrite(cargs[0]);
					break;
				case 'd': // DISASSEMBLE
					if(cargs[0] == -1) cargs[0] = *regs.pc; // default start: value of PC
					if(cargs[1] == -1) cargs[1] = 0x10; // default length: 0x10 bytes
					disasm(cargs[0],cargs[1]);
					break;
				case 'b': // BREAKPOINT command
					if(cargs[0] == -1) {
						if (breakpoint != -1) printf("Current Breakpoint is at &%04X.\n", breakpoint);
						printf("Please specify new breakpoint address; 0 to disable breakpoint.\n\n");
					} else {
						breakpoint = cargs[0]%0x10000;
					}
					break;
				case 'l': // load command
					readstr("Enter file name:", filename, 200);
					if (cargs[0] == -1) {
						printf("Start address:");
						scanf("%x", &(cargs[0]));
					}
					memload(filename, cargs[0]); 
					break;
				case 'w': // write command
					readstr("Enter file name:", filename, 200);
					if (cargs[0] == -1) {
						printf("Start address:");
						scanf("%x", &(cargs[0]));
					}
					if (cargs[1] == -1) {
						printf("Length:");
						scanf("%x", &(cargs[1]));
					}
					memsave(filename, cargs[0], cargs[1]);					
					break;
				case 'q':
					quit=1;
					*breakflag = 0;
					// Todo: quit the emulator
					break;
				case 'P': // set programm counter
					if (cargs[0] != -1) *regs.pc = cargs[0];
					break;
				case 'S': // set stack pointer
					if (cargs[0] != -1) *regs.sp = cargs[0];
					break; 
				case 'A': // set accu
					if (cargs[0] != -1) *regs.a = cargs[0];
					break;
				case 'F': // set flag register
					if (cargs[0] != -1) *regs.f = cargs[0];
					break;
				case 'B': // set B register
					if (cargs[0] != -1) *regs.b = cargs[0];
					break;
				case 'C': // set C register
					if (cargs[0] != -1) *regs.c = cargs[0];
					break;
				case 'D': // set D register
					if (cargs[0] != -1) *regs.d = cargs[0];
					break;
				case 'E': // set E register
					if (cargs[0] != -1) *regs.e = cargs[0];
					break;
				case 'H': // set H register
					if (cargs[0] != -1) *regs.h = cargs[0];
					break;
				case 'L': // set L register
					if (cargs[0] != -1) *regs.l = cargs[0];
					break;
				default:
					printf("Unknown command.\n");
					break;
			}
		} while(!quit);
	}
}

// hexdump
void dump(unsigned int addr, unsigned int length) {
	unsigned int i;
	char asc[17];
	unsigned char b;

	printf("\n");

	for(i=0;i<length;i++) {
		if((i%16)==0) { // Start of line
			printf("&%04X: ",(addr+i)%0x10000);
		}
		b = fetch((addr+i)%0x10000); // get byte from memory
		printf("%02X ", b);          // print hexcode
		if ((b>31) && (b<127)) asc[i%16] = b; else asc[i%16] = '.';             
		if((i%16)==15) { // End of line
			asc[16] = 0;
			printf(" %s\n", asc);	
		}
	}
	printf("\n");
}

// writes data to memory
void memwrite(unsigned int addr) {
	unsigned int b;
	int status;

	do {
		printf("&%04x=", addr);
		status = scanf("%x", &b);
		if (status > 0) fetch(addr) = (unsigned char)b;
		addr++;
	} while(status>0);
}

// load memory dump
int memload(char *filename, unsigned int addr) {
	FILE *memdump;
	unsigned char b;
	
	if((memdump = fopen(filename, "r")) == NULL) {
		return -1; // error opening file
	}
	
	while((addr <= 0xFFFF) && (!feof(memdump))) {
		b = getc(memdump);
		fetch(addr) = b;
		addr++;
	}
	
	fclose(memdump);
	return 0; // done
}

// save memory dump
int memsave(char *filename, unsigned int addr, unsigned int length) {
	FILE *memdump;
	
	if ((memdump = fopen(filename, "w")) == NULL) {
		return -1;
	}

	while((length >= 1) && (addr <= 0xFFFF)) {
		fputc(fetch(addr), memdump);
		addr++;
		length--;
	}
	fclose(memdump);
	
	return 0;
}


// disassemble memory
void disasm(unsigned int startaddr, unsigned int length) {
	unsigned int addr;
	unsigned char opcode, opcode2, prefix, index, data8;
	unsigned int data16;
	unsigned char x,y,z,p,q,d;
	char decoded[30]; // decoded opcode including paramerers
	char *decodeptr;
	
	addr = startaddr;
	
	do{
		printf("&%04X: ", addr); // print opcode address
	
		strcpy(decoded,"UNKNOWN");
		
		index = REG_HL; // no DD/FD prefix yet
		decodeptr = decoded;
		opcode = fetch(addr); // get opcode
		
		// DD prefix
		while(opcode == 0xDD){
			index = REG_IX;
			addr++;
			opcode = fetch(addr);
		}
		
		// FD prefix
		while(opcode == 0xFD){
			index = REG_IY;
			addr++;
			opcode = fetch(addr);
		} 
		
		switch(opcode) {
			case 0xCB: // CB prefixed
			case 0xED: // ED prefixed
				prefix = opcode;
				addr++;
				opcode2 = fetch(addr);
				break;
			default: // unprefixed
				prefix = 0x00;
				opcode2 = opcode;
				break;
		}
		
		x = opcode2 >> 6;
		y = (opcode2 & 0x3F) >> 3;
		z = (opcode2 & 0x07);
		p = y>>1;
		q = y%2;
		
		// Unprefixed opcodes
		if (prefix == 0x00) {
			switch(x) {
				case 0:
				switch(z) {
					case 0:
						switch(y) {
							case 0:
								WRITEOP("NOP");	
							break;
							case 1:
								WRITEOP("EX AF, AF'");
							break;
							case 2:
								addr++;
								data8 = fetch(addr);
								sprintf(decoded, "DJNZ &%04x", addr+1+(signed char)data8);
								break;
							case 3:
								addr++;
								data8 = fetch(addr);			
								sprintf(decoded, "JR &%04x", addr+1+(signed char)data8);
								break;
							default:
								addr++;
								data8 = fetch(addr);
								sprintf(decoded, "JR %s, &%04x", decode_cc[y-4], addr+1+(signed char)data8);
								break;
						}
					break; // (case z==0)
					case 1:
						if (q == 0) {
							addr+=2;
							data16 = fetch(addr-1) + fetch(addr)*256;
							sprintf(decoded, "LD %s, &%04x", decode_rp[p+index*4], data16);
						} else { // q == 1
							sprintf(decoded, "ADD HL, %s", decode_rp[p+index*4]);
						}
					break; // (case z==1)
					case 2:
						if (q == 0) {
							switch(p) {
								case 0:
									WRITEOP("LD (BC),A");
									break;
								case 1:
									WRITEOP("LD (DE),A");
									break;
								case 2:
									addr+=2;;
									data16 = fetch(addr-1) + fetch(addr)*256;
									sprintf(decoded, "LD (&%04x), HL", data16);
									break;
								case 3:
									addr+=2;
									data16 = fetch(addr-1) + fetch(addr)*256;
									sprintf(decoded, "LD (&%02x), A", data16);
									break;
							}
						} else { // q == 1
							switch(p) {
								case 0:
									WRITEOP("LD A,(BC)");
									break;
								case 1:
									WRITEOP("LD A,(DE)");
									break;
								case 2:
									addr+=2;
									data16 = fetch(addr-1) + fetch(addr)*256;
									sprintf(decoded, "LD HL, (&%04x)", data16);
									break;
								case 3:
									addr+=2;
									data16 = fetch(addr-1) + fetch(addr)*256;
									sprintf(decoded, "LD A, (&%04x)", data16);
									break;
							}
						}
					break; // (case z==2)
					case 3: // 16 Bit INC/DEC
						if (q==0) {
							sprintf(decoded, "INC %s", decode_rp[p+index*4]);
						} else { // q==1
							sprintf(decoded, "DEC %s", decode_rp[p+index*4]);
						}
					break; // (case z==3)
					case 4:
						if ((index>0)&&(y==6)) {
							addr++;
							d = fetch(addr);
							sprintf(decoded, "INC (%s+&%02x)", decode_idx[index], d);
						} else {
							sprintf(decoded, "INC %s", decode_r[y+index*8]);
						}
					break; // (case z==4)
					case 5:
						if ((index>0)&&(y==6)) {
							addr++;
							d = fetch(addr);
							sprintf(decoded, "DEC (%s+&%02x)", decode_idx[index], d);
						} else {
							sprintf(decoded, "DEC %s", decode_r[y+index*8]);
						}
					break; // (case z==5)
					case 6:
						if ((index>0)&&(y==6)) {
							addr++;
							d = fetch(addr);
							addr++;
							data8 = fetch(addr);
							sprintf(decoded, "LD (%s+&%02x), &%02x", decode_idx[index], d, data8);
						} else {
							addr++;
							data8 = fetch(addr);
							sprintf(decoded, "LD %s, &%02x", decode_r[y+index*8], data8);
						}
					break; // (case z==6)
					case 7:
						sprintf(decoded, "%s", decode_af[y]);
					break; // (case z==7)
				}
				break; // (case x==0)
				case 1:
					if ((y==6)&&(z==6)) {
						WRITEOP("HALT");
					} else {
						if ((index>0)&&(y==6)) {
							addr++;
							d = fetch(addr);
							sprintf(decoded, "LD (%s+&%02x), %s", decode_idx[index], d, decode_r[z]);
						} else if ((index>0)&&(z==6)) {
							addr++;
							d = fetch(addr);
							sprintf(decoded, "LD %s, (%s+&%02x)", decode_r[y], decode_idx[index], d);
						} else {
							sprintf(decoded, "LD %s, %s", decode_r[y+index*8], decode_r[z+index*8]);
						}
					}
				break; // (case x==1)
				case 2:
					if((index>0)&&(z==6)){
						addr++;
						d = fetch(addr);
						sprintf(decoded, "%s(%s+&%02x)", decode_alu[y], decode_idx[index], d);
					} else { 
						sprintf(decoded, "%s%s", decode_alu[y], decode_r[z+index*8]);
					}
				break; // (case x==2)
				case 3:
					switch(z) {
						case 0:
							sprintf(decoded, "RET %s", decode_cc[y]);
							break; // (case z==0)
						case 1:
							if(q==0) {
								sprintf(decoded, "POP %s", decode_rp2[p]);
							} else {
								sprintf(decoded, "%s", decode_var[p]);
							}
							break; // (case z==1)
						case 2:
							addr+=2;
							data16 = fetch(addr-1) + fetch(addr)*256;
							sprintf(decoded, "JP %s, &%04x", decode_cc[y], data16);
							break; // (case z==2)
						case 3:
							switch(y) {
								case 0:
									addr+=2;
									data16 = fetch(addr-1) + fetch(addr)*256;
									sprintf(decoded, "JP &%04x", data16);
									break;
								// 1 is CB prefix
								case 2:
									addr++;
									data8 = fetch(addr);
									sprintf(decoded, "OUT(&%02x), A", data8);
									break;
								case 3:
									addr++;
									data8 = fetch(addr);
									sprintf(decoded, "IN A, (&%02x)", data8);
									break;
								case 4:
									WRITEOP("EX (SP), HL");
									break;
								case 5:
									WRITEOP("EX DE, HL");
									break;
								case 6:
									WRITEOP("DI");
									break;
								case 7: 
									WRITEOP("EI");
									break;	
							}
							break; // (case z==3)
						case 4:
							addr+=2;
							data16 = fetch(addr-1) + fetch(addr)*256; 
							sprintf(decoded, "CALL %s, &%04x", decode_cc[y], data16);
							break; // (case z==4)	
						case 5:
							if(q==0) {
								sprintf(decoded, "PUSH %s", decode_rp2[p+index*4]);
							} else {
								if(p==0) {
									addr+=2;
									data16 = fetch(addr-1) + fetch(addr)*256;
									sprintf(decoded, "CALL &%04x", data16);
								}
							}
							break; // (case z==5)
						case 6:
							addr++;
							data8 = fetch(addr);
							sprintf(decoded, "%s&%02x", decode_alu[y], data8);
							break; // (case z==6)
						case 7:
							sprintf(decoded, "RST &%02x", y*8);
							break;	
					}
				break; // (case x==3)
			}
		} // unprefixed opcode decoding
		
		if ((prefix == 0xCB) && (index == 0)) // &CB-prefixed opcodes 
		{
			switch(x) {
				case 0:
					sprintf(decoded, "%s %s", decode_rot[y], decode_r[z]);
					break;
				case 1:
					sprintf(decoded, "BIT %i, %s", y, decode_r[z]);
					break;
				case 2:
					sprintf(decoded, "RES %i, %s", y, decode_r[z]);
					break;
				case 3:
					sprintf(decoded, "SET %i, %s", y, decode_r[z]);
					break;
			}
		} // &CB-prefixed opcodes
		
		if ((prefix == 0xCB) && (index > 0)) // &DDCB/&FDCB-prefixed opcodes
		{
			switch(x) {
				case 0:
					addr++;
					d = fetch(addr);
					if (z == 6) {	
						sprintf(decoded, "%s (%s+&%02x)", decode_rot[y], decode_idx[index], d);
					} else {
						sprintf(decoded, "LD %s, %s (%s+&%02x)", decode_r[z], decode_rot[y], decode_idx[index], d);
					}
					break;
				case 1:
					sprintf(decoded, "BIT %i, %s", y, decode_r[z]);
					break;
				case 2:
					addr++;
					d = fetch(addr);
					if (z == 6) {
						sprintf(decoded, "RES %i, (%s+&%02x)", y, decode_idx[index], d);
					} else {
						sprintf(decoded, "LD %s, RES %i, (%s+&%02x)", decode_r[z], y, decode_idx[index], d);
					}
					break;				
				case 3:
					addr++;
					d = fetch(addr);
					if (z == 6) {
						sprintf(decoded, "SET %i, (%s+&%02x)", y, decode_idx[index], d);
					} else {
						sprintf(decoded, "LD %s, SET %i, (%s+&%02x)", decode_r[z], y, decode_idx[index], d);
					}
					break;				
			}
		} // &DDCB/&FDCB-prefixed opcodes
		
		if (prefix == 0xED) // &ED-prefixed opcodes
		{
			switch(x) {
				case 1:
					switch(z) {
						case 0:
							if (y==6) {
								WRITEOP("IN (C)");
							} else {
								sprintf(decoded, "IN %s, (C)", decode_r[y]);
							}
							break;
						case 1:
							if (y==6) {
								WRITEOP("OUT (C), 0");
							} else {
								sprintf(decoded, "OUT (C), %s", decode_r[y]);
							}
							break;
						case 2:
							if (q==0) {
								sprintf(decoded, "SBC HL, %s", decode_rp[p]);
							} else {
								sprintf(decoded, "ADC HL, %s", decode_rp[p]);
							}
							break;
						case 3:
							addr+=2;
							data16 = fetch(addr-1) + fetch(addr)*256;
							if (q==0) {
								sprintf(decoded, "LD (&%04x), %s", data16, decode_rp[p]);
							} else {
								sprintf(decoded, "LD %s, (&%04x)", decode_rp[p], data16);
							}
							break;
						case 4:
							WRITEOP("NEG");
							break;
						case 5:
							if(y==1) {
								WRITEOP("RETI");
							} else {
								WRITEOP("RETN");
							}
							break;
						case 6:
							sprintf(decoded, "IM %s", decode_im[y%4]);
							break;
						case 7:
							sprintf(decoded, "%s", decode_ed7[y]);
							break;
					}
					break; // (case x==1)
				case 2:
					if ((y>=4) && (z<=3)) sprintf(decoded, "%s", decode_bli[(y-4)*4+z]);
					break; // (case x==2)
				default: // no instruction
					break;
			}
		}
		
		printf("%s\n", decoded);
		addr++;
	} while(addr-startaddr < length);	
		
} /* disasm() */

void readstr(char *prompt, char *stringvar, int maxlen) { /* read string from STDIN */
	/* Code */
	do {
		printf("%s", prompt);
		fgets(stringvar, maxlen, stdin);
		if (stringvar[strlen(stringvar)-1] == '\n')
			stringvar[strlen(stringvar)-1] = 0;
	} while (strlen(stringvar) == 0);
}

int parsecmd(char *cmdln, int *cargs) {
	int i;
	int cmdlen;
	int n=0;
	int base;
	int success=1;
	char *cargv[MAXPARAM];
	char *remainder;
	
	// initialize *cargs[] array
	for(i=0;i<MAXPARAM;i++) cargs[i] = -1; 
	
	cmdlen = strlen(cmdln);
	for(i=0;(i<cmdlen)&&(n<MAXPARAM);i++) {
		if((cmdln[i] == ' ')||(cmdln[i] == '=')){
			cmdln[i] = 0; // split string by replacing spaces with End-of-String marks
			cargv[n] = &cmdln[i+1]; // save pointer to string-part
			n++;
		}
	}
	
	for(i=0;i<n;i++) {
		if(cargv[i][0] == '&') { // hex value
			cargv[i]++; // jump over first character ('&')
			base = 16;
		} else { // decimal value
			base = 10;
		}
		cargs[i] = (int)strtol(cargv[i], &remainder, base); // convert string to number
		if(strlen(remainder)>0) success = 0;
		//printf("parameter %i: %i\n",i, cargs[i]); // for debugging
	}
	
	return success; // parsing successful
}
