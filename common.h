/* nc100em, an Amstrad NC100 emulator.
 * Copyright (C) 1994 Ian Collier. nc100em changes (C) 1996,1999 Russell Marks.
 *
 * common.h
 */

extern unsigned char mem[];
extern unsigned char *cardmem;
extern unsigned char *dfile;
extern unsigned char *memptr[];
extern unsigned long tstates,tsmax;
extern int memattr[];
extern volatile int signal_int_flag;
extern int scrn_freq;
extern int default_scale;

extern int card_size;
extern int card_status;
extern int irq_status;
extern int irq_mask;
extern int scrnaddr;
extern int do_nmi;
extern int force_full_redraw;
extern int allow_serial;

extern unsigned char keyports[];
extern float freq1, freq2; 

extern int nc200;
extern int nc150;


extern void writeram(unsigned char *x);
extern void writecard(unsigned char *x);
extern unsigned int in(int h,int l);
extern unsigned int out(int h,int l,int a);
extern void serial_uninit(void);
extern int serial_input_pending(void);
extern void serout_flush(void);
extern void common_init(void);
extern void startsigsandtimer(void);
extern void parseoptions(int argc,char *argv[]);
