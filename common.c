/* nc100em, an Amstrad NC100 emulator.
 * Copyright (C) 1994 Ian Collier. nc100em changes (C) 1996,1999 Russell Marks.
 *
 * common.c - bits common to all versions.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "common.h"
#include "z80.h"
#include "libdir.h"
#include "pdrom.h"
#include "fdc.h"

unsigned short pc; /* make pc extern for debugging */
int breakpoint;

/* name of file for printer output (appended to if any output received) */
#define PRINTER_OUTPUT	"printout.dat"


/* the memory is (up to) 512k ROM and 128k RAM */
unsigned char mem[(512+128)*1024];
unsigned char *cardmem=NULL;	/* malloc'd or mmap'd PCMCIA card mem */
unsigned char *dfile;
/* boots with all ROM0 */
unsigned char *memptr[4]={mem,mem,mem,mem};
unsigned char lastpageout[4]={0,0,0,0};	/* last outs to paging ports */
unsigned long tstates=0,tsmax=46060;

int memattr[4]={0,0,0,0};	/* all ROM at boot */

volatile int signal_int_flag=0;
int scrn_freq=5;
int default_scale=SCALE;

int card_size=0;		/* size of pcmcia card, or 0 if none */
int card_status=0;		/* as read from port 0xA0 */
int irq_status=0xff;		/* as read from port 0x90 */
int irq_mask=0;			/* as set via port 0x60 */
int scrnaddr=0xf000;		/* slightly nicer than assuming zero :-) */
int do_nmi=0;
int force_full_redraw=1;
int force_pd_rom=0;
int using_pd_rom=0;
int allow_serial=0;
int alt_colour_msg=0;	/* alter ROM colour msgs (e.g. "RED" -> "LEFT") */

unsigned char keyports[10]={0,0,0,0,0, 0,0,0,0,0};

unsigned char sound[4]={0xff, 0xff, 0xff, 0xff}; /* sound generator registers */
float freq1 = 0, freq2 = 0; /* sound generator frequency 1 and 2 */

int tty_fd=0;				/* use stdin */
unsigned char seroutbuf[16384];		/* won't need this many - defensive */
int seroutpos=0;

static char binfile_name[1024]="";
static int do_munmap=1;

FILE *printout=NULL;

int nc200=0;	/* default to NC100 */
int nc150=0;



/* the card_status flag never changes after this */
void set_card_status(void)
{
card_status=0x31; /* no batteries low, and parallel always ACK and !BUSY */
		  /* (note that nciospec.doc documents BUSY wrongly!) */
if(nc200)
  card_status=0x10;	/* the '200 has a different take on it  (use 0x11 for floppy disabled due to low batteries) */

if(card_size==0) card_status|=0x80;	/* set bit 7 if no card present */
}


void loadrom(unsigned char *x)
{
static unsigned char rom100timechk[]={0xc6,7,1,1,0,0,0};
FILE *in;

if(!force_pd_rom &&
   (in=fopen(libdir(nc200?"nc200.rom":(nc150?"nc150.rom":"nc100.rom")),"rb"))!=NULL)
  {
  fread(x,1024,(nc200|nc150)?512:256,in);
  fclose(in);
  }
else
  {
  if(!force_pd_rom)
    fprintf(stderr,"%s: no ROM found, using builtin PD `ROM'.\n",cmd_name);
  memcpy(x,pd_rom,sizeof(pd_rom));
  using_pd_rom=1;
  return;
  }

/* if it's ROM v1.00, patch it so it doesn't do an is-time-set check
 * which would be a pain to emulate.
 * (Unfortunately, I *still* seem to need this, even with the improved
 * RTC emulation...)
 *
 * Patching not neccessary anymore. RTC initialized with better default values.
 */

//if(!nc200 && memcmp(x+0x9c0,rom100timechk,7)==0)
//  {
//   x[0xaa2]=0x37;	/* scf */
//   x[0xaa3]=0xc9;	/* ret */
//  }

/* if enabled, change ROM colour-key messages to be less impenetrable.
 * XXX this won't work on the '200
 */
if(!nc200 && alt_colour_msg)
  {
  static char *orig[6]=
    {
    "  YELLOW & RED   ",
    "      RED        ",
    " YELLOW & GREEN  ",
    "      GREEN      ",
    "  YELLOW & BLUE  ",
    "      BLUE       "
    };
  static char *changed[6]=
    {
    "  INSERT & LEFT  ",
    "      LEFT       ",
    " INSERT & RIGHT  ",
    "      RIGHT      ",
    "  INSERT & DOWN  ",
    "      DOWN       "
    };
  int f,addr,found;
  
  for(f=0;f<6;f++)
    {
    /* really sophisticated search algorithm :-) */
    for(addr=0x4000,found=0;addr<0x4400;addr++)
      if(mem[addr]==orig[f][0])
        if(strncmp(mem+addr,orig[f],18)==0)
          strcpy(mem+addr,changed[f]),found=1;
    
    if(!found)
      fprintf(stderr,"%s: couldn't change \"%s\" colour key message\n",
      	cmd_name,orig[f]);
    }
  }
}


void loadram(unsigned char *x)
{
FILE *in;
char *ram_file=libdir(nc200?"nc200.ram":(nc150?"nc150.ram":"nc100.ram"));

if((in=fopen(ram_file,"rb"))!=NULL)
  {
  fread(x,1024,(nc200|nc150)?128:64,in);
  fclose(in);
  }
else
  {
  fprintf(stderr,"%s: couldn't load RAM file `%s', using blank.\n",
  	cmd_name,ram_file);
  /* not fatal - blank it and carry on */
  memset(x,0,((nc200|nc150)?128:64)*1024);
  }
}


unsigned char *loadcard(void)
{
FILE *in;
unsigned char *cardmem;
char *card_file=libdir(nc200?"nc200.card":(nc150?"nc150.card":"nc100.card"));

/* we open read/write so mmap will work properly */
if((in=fopen(card_file,"r+"))!=NULL)
  {
  fseek(in,0,SEEK_END);
  card_size=ftell(in)/1024;
#ifndef USE_MMAP_FOR_CARD
  rewind(in);
  if((cardmem=malloc(card_size*1024))==NULL)
    fprintf(stderr,"%s: out of memory.\n",cmd_name),exit(1);
  fread(cardmem,1024,card_size,in);
  fclose(in);
#else
  if((cardmem=mmap(0,card_size*1024,PROT_READ|PROT_WRITE,
  	MAP_SHARED,fileno(in),0))==MAP_FAILED)
    {
    fprintf(stderr,"%s: couldn't mmap() mem card file `%s'.\n",
    	cmd_name,card_file);
    exit(1);
    }
#endif /* USE_MMAP_FOR_CARD */
  }
else /* if fopen failed */
  {
  fprintf(stderr,"%s: couldn't open mem card file `%s'.\n",
  	cmd_name,card_file);
  /* not fatal, get a blanked 1024k */
  do_munmap=0;	/* don't try to munmap this! */
  if((cardmem=calloc(card_size*1024,1))==NULL)
    fprintf(stderr,"%s: out of memory.\n",cmd_name),exit(1);
  }

return(cardmem);
}


void writeram(unsigned char *x)
{
FILE *out;
char *ram_file=libdir(nc200?"nc200.ram":(nc150?"nc150.ram":"nc100.ram"));

if((out=fopen(ram_file,"wb"))==NULL)
  fprintf(stderr,"%s: couldn't write RAM to `%s'.\n",cmd_name,ram_file);
else
  {
  fwrite(x,1024,(nc200|nc150)?128:64,out);
  fclose(out);
  }
}


void writecard(unsigned char *x)
{
char *card_file=libdir(nc200?"nc200.card":(nc150?"nc150.card":"nc100.card"));

/* we also close any open printer file while we're at it :-) */
if(printout) fclose(printout);

#ifdef USE_MMAP_FOR_CARD

if(do_munmap && munmap(x,card_size*1024)==-1)
  fprintf(stderr,"%s: couldn't munmap() mem card file `%s'.\n",
  	cmd_name,card_file);

#else
FILE *out;

if(card_size>0)
  {
  if((out=fopen(card_file,"wb"))==NULL)
    fprintf(stderr,"%s: couldn't write mem card to `%s'.\n",
    	cmd_name,card_file);
  else
    {
    fwrite(x,1024,card_size,out);
    fclose(out);
    }
  }
#endif /* !USE_MMAP_FOR_CARD */
}


/* I/O, quoting from nciospec.doc:
[we ignore some]
D0-DF                   RTC (TC8521)            R/W	(ours is read-only)
C0-C1                   UART (uPD71051)         R/W
B0-B9                   Key data in             R
A0                      Card Status etc.        R
90                      IRQ request status      R/W
70                      Power on/off control    W
60                      IRQ Mask                W
50-53                   Speaker frequency       W	(ignored)
40                      Parallel port data      W
30                      Baud rate etc.          W	(ignored)
20                      Card wait control       W	(ignored)
10-13                   Memory management       R/W
00                      Display memory start    W
*/


void do_paging(int page,int n)
{
lastpageout[page]=n;

switch(n&0xc0)
  {
  case 0x00:	/* ROM */
    memptr[page]=mem+16384*n;
    memattr[page]=0;
    break;
  case 0x40:	/* RAM */
    memptr[page]=mem+RAM_START+16384*((n&0x0f)%8); // never EVER write to memory location outside of the 128K
    memattr[page]=1;
    break;
  case 0x80:	/* card ram */
    memptr[page]=cardmem+16384*(n&0x3f);
    memattr[page]=1;
    /* one way of detecting the size of a card requires
     * that writes which are off-card must fail, so...
     */
    if(16*(n&0x3f)>=card_size) memattr[page]=0;
    break;
  }
}


void serial_init(void)
{
/* XXX should do this properly... */
/* mind you this is probably more portable than termios :-) */

if(!allow_serial) return;

/* the -echo is really for tnc100em, but doesn't hurt anyway */
system("stty raw -echo");

/* make stdin non-blocking */
fcntl(0,F_SETFL,O_NONBLOCK);
}


void serial_uninit(void)
{
if(!allow_serial) return;

/* make stdin blocking again */
fcntl(0,F_SETFL,0);

/* XXX also crap */
system("stty -raw echo");
}


int serial_input_pending(void)
{
struct timeval tv;
fd_set fds;

if(!allow_serial) return(0);

tv.tv_sec=0; tv.tv_usec=0;
FD_ZERO(&fds); FD_SET(tty_fd,&fds);
if(select(tty_fd+1,&fds,NULL,NULL,&tv)<=0)
  return(0);

return(FD_ISSET(tty_fd,&fds));
}


int serial_output_allowed(void)
{
#if 1
/* assume it always is. select() takes yonks in our terms. */
return(1);
#else
struct timeval tv;
fd_set fds;

tv.tv_sec=0; tv.tv_usec=0;
FD_ZERO(&fds); FD_SET(tty_fd,&fds);
select(tty_fd+1,NULL,&fds,NULL,&tv);

return(FD_ISSET(tty_fd,&fds));
#endif
}


int get_serial_byte(void)
{
unsigned char c=0;

if(!allow_serial) return(0);

if(read(tty_fd,&c,1)<=0)
  return(0);

return((int)c);
}


void put_serial_byte(int n)
{
unsigned char c=n;

#if 0
if(serial_output_allowed())
#endif
  seroutbuf[seroutpos++]=c;
}


void serout_flush(void)
{
if(!allow_serial) return;

if(seroutpos>0)
  write(tty_fd,seroutbuf,seroutpos);
seroutpos=0;
}


void put_printer_byte(int n)
{
static int printout_open_failed=0;
int bit=(nc200?1:4);

if(!printout && !printout_open_failed)
  {
  if((printout=fopen(libdir(PRINTER_OUTPUT),"ab"))==NULL)
    {
    printout_open_failed=1;	/* don't slow to crawl by trying every time */
    return;
    }
  }

fputc(n,printout);

/* now, the ROM (somewhat stupidly IMHO) requires the printer-ACK
 * interrupt in order to work. We emulate it in a similar way to
 * a serial-input int, except that in this case irq_status
 * has the relevant bit cleared here.
 */
if((irq_mask&bit)) irq_status&=~bit;
}


/* NC100 RTC code.
 *
 * this supports all four pages (time, alarm, and two spare RAM pages).
 * It has many limitations though; it doesn't support:
 *
 * - the 1Hz and 16Hz timers
 * - alarms (any attempt to set one is ignored)
 * - setting the time (ditto - you'd need to be root anyway :-))
 * - 12hr operation (24hr only)
 * - the undocumented `test' register (reads as zero, writes ignored)
 */
unsigned int rtc_inout(int is_out,int h,int l,int a)
{
static int page=0,page_reg=0;	/* page num and page register */
static int page2[13],page3[13]={6,7,1,1,0,0,0,0,0,0,0,0,0}; // default values indicating an initialized RTC
static struct tm *rtc_tm=NULL;
int pad=0xf0;	/* high nibble value */
time_t timet;

if(is_out)
  {
  switch(l)
    {
    case 0xdd:		/* page register */
      /* timer/alarm enable are ignored */
      page=(a&3);
      page_reg=a;
      
      /* we take the opportunity to read the time, assuming this port
       * is written to just before the time is read (to make sure the
       * time page is selected); anyway, both the ROM OS and ZCN do this,
       * so in practice it's good enough. :-)
       */
      timet=time(NULL);
      rtc_tm=localtime(&timet);
      break;
    
    case 0xde:		/* test register - ignored */
      break;
    
    case 0xdf:		/* reset register - ignored */
      break;
    
    default:		/* write to RAM */
      if(page<2)
        break;		/* ignore writes in time/alarm pages */
      if(page==2)
        page2[l&15]=a;
      else
        page3[l&15]=a;
    }
  
  return(0);
  }

/* otherwise it's an in. */


/* the chip deals in 4-bit values, and on an NC the top bits are
 * usually all set (but not always!), so we or with 0xf0.
 */
if(l==0xdd)
  return(pad|page_reg);

if(l==0xde || l==0xdf)	/* write-only, ret 0 */
  return(pad);

switch(page)
  {
  case 0:		/* time page */
    /* XXX from old TODO (is this still true, or was it an NC200 thing?):
     * Need to fix RTC. My kludge which grabbed rtc_tm isn't being triggered
     * any more, so the time doesn't work.
     */
    if(!rtc_tm) return(pad);
    
    switch(l)
      {
      case 0xd0:	return(pad|(rtc_tm->tm_sec%10));
      case 0xd1:	return(pad|(rtc_tm->tm_sec/10));
      case 0xd2:	return(pad|(rtc_tm->tm_min%10));
      case 0xd3:	return(pad|(rtc_tm->tm_min/10));
      case 0xd4:	return(pad|(rtc_tm->tm_hour%10));
      case 0xd5:	return(pad|(rtc_tm->tm_hour/10));
      case 0xd6:	return(pad|(rtc_tm->tm_wday));
      case 0xd7:	return(pad|(rtc_tm->tm_mday%10));
      case 0xd8:	return(pad|(rtc_tm->tm_mday/10));
      case 0xd9:	return(pad|((rtc_tm->tm_mon+1)%10));
      case 0xda:	return(pad|((rtc_tm->tm_mon+1)/10));
      case 0xdb:	return(pad|((rtc_tm->tm_year-90)%10));
      case 0xdc:	return(pad|((rtc_tm->tm_year-90)/10));
      default:		return(pad);
      }
  
  case 1:		/* alarm page */
    switch(l)
      {
      case 0xda:
        return(pad|1);	/* set low bit since we always use 24hr */
      case 0xdb:
        if(!rtc_tm) return(pad);		/* XXX see earlier note */
        return(pad|(rtc_tm->tm_year&3));	/* good until 2099 */
      case 0xdc:
        /* this identifies to a clueful program that it's running
         * under nc100em, as needed for the co-operative mouse support
         * `emulation'. This is an otherwise-unused (and useless)
         * byte, so this shouldn't break anything.
         */
        return(100);	/* as in NC~ :-) */
      default:
        return(pad);	/* otherwise zero */
      }
  
  case 2:
    return(pad|page2[l&15]);
  
  case 3:
    return(pad|page3[l&15]);
  }

/* can't get here, but just in case... */
return(pad|0xf);
}


/* NC200 RTC code.
 * It seems to use a different chip from the NC100, which I haven't
 * identified, so I'm basically stuck with minimal reverse-engineered
 * info on this one - still, this is apparently good enough to get the
 * ROM's time/date-reading to work.
 */
unsigned int rtc200_inout(int is_out,int h,int l,int a)
{
static struct tm *rtc_tm=NULL;
static int reg=0;
time_t timet;

if(l<0xd0 || l>0xd1) return(0);

if(is_out && l==0xd0)
  {
  reg=(a&0x0f);
  return(0);
  }

if(l==0xd0) return(0);

/* so, l==0xd1. */
if(is_out)
  {
  return(0);
  }

/* otherwise it's an in. */

switch(reg)
  {
  /* 1,3,5 seem to be alarm-related */
  case 0:	timet=time(NULL),rtc_tm=localtime(&timet);
  		return(rtc_tm?rtc_tm->tm_sec:0);
  case 2:	return(rtc_tm?rtc_tm->tm_min:0);
  case 4:	return(rtc_tm?rtc_tm->tm_hour:0);
  case 7:	return(rtc_tm?rtc_tm->tm_mday:0);
  case 8:	return(rtc_tm?(rtc_tm->tm_mon+1):0);
  case 9:	return(rtc_tm?(rtc_tm->tm_year-90):0);

  case 0xb:	return(4);	/* these both seem to be needed... */
  case 0xd:	return(128);	/* ...for it to think the time's set */
  
  default:	return(0);
  }

/* can't get here, but just in case... */
return(0);
}

void set_soundfreq(void) /* calculate frequencies for sound generator */
{
#define SOUND_OFF 0x80

 /* sound channel 1 */
 if ((sound[1] & SOUND_OFF) || (sound[0] + sound[1] == 0)) /* MSB set = sound channel 1 off */
 {
 	freq1 = 0; /* freq2 = 0 => sound channel turned off */
#ifdef DEBUG
 	printf("Sound channel 1 off\n");
#endif
 }
 else /* sound channel 1 on */
 {
 	freq1 = (float)1000000 / ((float)(sound[0] + sound[1]*256) * (float)2 * (float)1.6276);
#ifdef DEBUG
 	printf("Sound channel 1: %fHz\n", freq1);
#endif
 }

 /* sound channel 2 */
 if ((sound[3] & SOUND_OFF) || (sound[2] + sound[3] == 0)) /* MSB set = sound channel 2 off */
 {
 	freq2 = 0;  /* freq1 = 0 => sound channel turned off */
#ifdef DEBUG
 	printf("Sound channel 2 off\n");
#endif
 }
 else /* sound channel 2 on */
 {
 	freq2 = (float)1000000 / ((float)(sound[2] + sound[3]*256) * (float)2 * (float)1.6276);
#ifdef DEBUG
 	printf("Sound channel 2: %fHz\n", freq2);
#endif
 }
}

unsigned int in(int h,int l)
{
static int ts=(13<<8);	/* num. t-states for this out, times 256 */
static int mx=0,my=0,mb=0;

// Print Port number, but ignore keyboard reads
// if(((l<0xB0)||(l>0xB9)) &&(l!=0x90))printf("***DEBUG*** Read from Port: &%02X\n", l);

/* h is ignored by the NC100 */
switch(l)
  {
  /* Floppy */
  case 0xe0: // Status regitser
  	/* debug: show location of memory read*/
  	// printf("Memory: 1:&%04x 2:&%04x 3:&%04x 4:&%04x\n",(memptr[0]-mem)/16384,(memptr[1]-mem)/16384,(memptr[2]-mem)/16384,(memptr[3]-mem)/16384);
  	return (ts|fdc_getmainstatus());
  	// return(ts|fdc_getmainstatus()); // Invertiertes Statusregister
  case 0xe1: /// Data register
  	return(ts|fdc_read());

  /* RTC */
  case 0xd0: case 0xd1: case 0xd2: case 0xd3:
  case 0xd4: case 0xd5: case 0xd6: case 0xd7:
  case 0xd8: case 0xd9: case 0xda: case 0xdb:
  case 0xdc: case 0xdd: case 0xde: case 0xdf:
    return(ts|(nc200?rtc200_inout(0,h,l,0):rtc_inout(0,h,l,0)));
  
  /* UART */
  case 0xc0:	/* this reads from the serial port */
    return(ts|get_serial_byte());
  
  case 0xc1:    /* returns bit 0=1 if can send a byte */
    /* we assume it's always possible */
    return(ts|1);
    
  /* keyboard */
  case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4:
  case 0xb5: case 0xb6: case 0xb7: case 0xb8: case 0xb9:
    /* reading 0xb9 also sets bit 3 of irq_status */
    if(l==0xb9) irq_status|=8;
    return(ts|keyports[l-0xb0]);
  
  case 0xa0:	/* card etc. status */
    return(ts|card_status);
  
  case 0x90:	/* IRQ status */
      	// printf("Memory: 1:&%04x 2:&%04x 3:&%04x 4:&%04x\n",(memptr[0]-mem)/16384,(memptr[1]-mem)/16384,(memptr[2]-mem)/16384,(memptr[3]-mem)/16384);
	// printf("INT SOURCE requested at PC=&%04X\n", pc);
    return(ts|irq_status);
  
  case 0x80:	/* NC200 printer busy */
    if(nc200) return(ts);	/* zero */
    return(ts|255);
  
  /* memory paging */
  case 0x10: return(ts|lastpageout[0]);
  case 0x11: return(ts|lastpageout[1]);
  case 0x12: return(ts|lastpageout[2]);
  case 0x13: return(ts|lastpageout[3]);
  
  /* co-operative mouse `emulation' - doesn't emulate a mouse, but does
   * provide a way to read the pointer location for nc100em-aware programs.
   * zcnpaint (and zcnlib) as of ZCN 1.3 support this, for example.
   */
  case 0x2a:
    {
    /* grab current state of pointer, and read buttons. You should read
     * this port before reading the other three.
     */
    static struct timeval last_read={0,0};
    struct timeval now;
    int old,new;
    int l,m,r;
    
    /* limit it to max of 500 times/sec - without this you could really
     * give your X server hell. :-)
     */
    gettimeofday(&now,NULL);
    old=last_read.tv_usec;
    new=now.tv_usec;
    if(now.tv_sec>last_read.tv_sec)
      new+=1000000;
    if(new-old<=2000)
      return(ts|mb);
    
    last_read.tv_sec=now.tv_sec;
    last_read.tv_usec=now.tv_usec;
    
    get_mouse_state(&mx,&my,&mb);
    
    l=( mb    &1);
    m=((mb>>1)&1);
    r=((mb>>2)&1);
    
    /* there are multiple bit layouts in the returned byte, to allow
     * the most usual bit layouts to be read directly without needing
     * the caller to do hairy bit-twiddling. The byte layout is:
     *
     * 76543210
     * 0lmrmlrl  ...with l=left, m=middle, r=right mouse buttons.
     */
    mb=((l<<6)|(m<<5)|(r<<4)|(m<<3)|(l<<2)|(r<<1)|l);
    
    return(ts|mb);
    }
  
  case 0x2b:	/* read x low */
    return(ts|(mx&255));
  
  case 0x2c:	/* read x high */
    return(ts|(mx>>8));
  
  case 0x2d:	/* read y low */
    return(ts|my);
  default:
  	printf("IN from unknown Port: &%02x\n", l);
  	break;
  }

/* otherwise... */
return(ts|255);
}


unsigned int out(int h,int l,int a)
{
static int printer_byte=0;	/* data output to port 0x40 */
static int printer_strobe=1;	/* normal state is high */
static int ts=13;		/* num. t-states for this out */

// printf("***DEBUG*** Port output to &%02X, Value &%02X\n", l,a);

/* h is ignored by the NC100 */
switch(l)
  {
  /* Floppy */
  case 0xe1: // Data register
  	fdc_write(a);
  	return(ts);
  
  /* RTC */
  case 0xd0: case 0xd1: case 0xd2: case 0xd3:
  case 0xd4: case 0xd5: case 0xd6: case 0xd7:
  case 0xd8: case 0xd9: case 0xda: case 0xdb:
  case 0xdc: case 0xdd: case 0xde: case 0xdf:
    nc200?rtc200_inout(1,h,l,a):rtc_inout(1,h,l,a);
    return(ts);
  
  /* UART */
  case 0xc0:	/* this writes to the serial port */
    put_serial_byte(a);
    return(ts);
  
  case 0xc1:    /* sets up various serial parms which we ignore */
    // printf("Serial config: %02X\n", a);
    return(ts);
  
  case 0x90:	/* IRQ status */
    /* when a zero is written to a bit, it should re-enable that IRQ line */
    irq_status|=(a^255);
    return(ts);
  
  case 0x70:	/* power on/off */
    /* if bit 0 is 0, turn off */
    /* XXX the '200 seems to have bits 1 and 2 meaning something too? */
    if((a&1)==0) dontpanic_nmi();
    return(ts);
  
  case 0x60:	/* set irq mask */
    irq_mask=a;
    return(ts);
  
  case 0x50: case 0x51: case 0x52: case 0x53:
    /* speaker frequency */
    sound[l-0x50] = a;
    set_soundfreq(); /* calculate soud generator frequencies */
    return(ts);
  
  case 0x40:	/* parallel port data */
    printf("Paralle port write: %i\n", a);
    printf("Memory: 1:&%04x 2:&%04x 3:&%04x 4:&%04x\n",(memptr[0]-mem)/16384,(memptr[1]-mem)/16384,(memptr[2]-mem)/16384,(memptr[3]-mem)/16384);
    printer_byte=a;	/* not written until strobe is pulsed */
    return(ts);
  
  case 0x30:	/* baud rate etc., ignored except for printer strobe */
    /* bit 6 is the strobe - low then high means print byte. */
    printf("Baud Rate Generator: %02X\n", a); 
    if(!printer_strobe && (a&0x40))
      put_printer_byte(printer_byte);
    printer_strobe=(a&0x40);
    return(ts);
  
  case 0x20:	/* card wait control, ignored */
  	if(nc200 && (a&0x01)) // Is this an NC200 and the TerminalCount Bit is set?
  		fdc_terminalcount(); // send terminal count pulse to FDC
    return(ts);
  
  /* memory paging */
  case 0x10: do_paging(0,a); return(ts);
  case 0x11: do_paging(1,a); return(ts);
  case 0x12: do_paging(2,a); return(ts);
  case 0x13: do_paging(3,a); return(ts);
  
  case 0:	/* set screen addr */
    scrnaddr=(a&0xf0)*256;
    force_full_redraw=1;	/* override differential updating */
    return(ts);
  }

/* otherwise... */
return(ts);
}



void sighandler(int a)
{
signal_int_flag=1;
}


/* init memory etc. */
void common_init(void)
{
loadrom(mem);
loadram(mem+RAM_START);
cardmem=loadcard();
set_card_status();

/* TEMPORARY CODE: Open example disc image */
fdc_insertdisc();

/* XXX need to check for '200 compat (eh?) */

/* load file at 100h if they specified one. */
if(*binfile_name)
  {
  FILE *in;
  
  /* only if it's the PD ROM */
  if(!using_pd_rom)
    fprintf(stderr,
    	"%s: can only load a file to boot with PD ROM (try `-p').\n",
    	cmd_name);
  else
    {
    /* blast the context-save magic so it jumps to 100h */
    memset(mem+RAM_START+0xb200,0,4);
    
    if((in=fopen(binfile_name,"rb"))==NULL)
      fprintf(stderr,"%s: couldn't open file `%s'.\n",cmd_name,binfile_name);
    else
      fread(mem+RAM_START+0x100,1,49152,in),fclose(in);
    }
  }

serial_init();
}


void startsigsandtimer(void)
{
struct sigaction sa;
struct itimerval itv;
int tmp=1000/100;	/* 100 ints/sec */

sigemptyset(&sa.sa_mask);
sa.sa_handler=dontpanic;
#ifndef __APPLE__
sa.sa_flags=SA_ONESHOT; /* Does not work on OS X */
#endif

sigaction(SIGINT, &sa,NULL);
sigaction(SIGHUP, &sa,NULL);
sigaction(SIGILL, &sa,NULL);
sigaction(SIGTERM,&sa,NULL);
sigaction(SIGQUIT,&sa,NULL);
sigaction(SIGSEGV,&sa,NULL);

sigemptyset(&sa.sa_mask);
sa.sa_handler=sighandler;
sa.sa_flags=SA_RESTART;

sigaction(SIGALRM,&sa,NULL);

itv.it_value.tv_sec=  0;
itv.it_value.tv_usec= 100000;
itv.it_interval.tv_sec=  tmp/1000;
itv.it_interval.tv_usec=(tmp%1000)*1000;
setitimer(ITIMER_REAL,&itv,NULL);
}


void usage_help(void)
{
printf("%s " NC100EM_VER " - "
	"(c) 1994-2000 Ian Collier and Russell Marks.\n",
	cmd_name);
printf("  %s is the ",cmd_name);
switch(*cmd_name)
  {
  case 'g': printf("GTK+"); break;
  case 's': printf("svgalib"); break;
  case 'x': printf("Xlib"); break;
  case 'd': printf("SDL"); break;
  default:
    printf("tty");
  }
printf("-based version of nc100em.\n\n");

printf("usage: %s [-d25hmps] [-r refresh_rate] [-S scale] "
	"[file_to_boot.bin]\n\n",cmd_name);
printf("\n"
"\t-d\truns NC100em in debug mode.\n"
"\t-2\temulate an NC200, rather than an NC100.\n"
"\t-5\temulate an NC150, rather than an NC100.\n"
"\t-h\tthis usage help.\n"
"\t-m\tmodify `colour' key messages in ROM, to make them more\n"
"\t\tunderstandable when using nc100em. For example, with `-m',\n"
"\t\tnc100em changes \"YELLOW & RED\" to \"INSERT & LEFT\".\n"
"\t\tCurrently this doesn't work in NC200 mode.\n"
"\t-p\tuse the builtin PD `ROM' even if `nc100.rom' is present.\n"
"\t\tUseful if you want to boot a file.\n"
"\t-r\tset how often the screen is redrawn in 1/100ths of a second\n"
"\t\t(5 by default).\n"
"\t-s\tenable serial I/O on stdin/stdout. Only relevant to\n"
"\t\tX versions (not supported in svgalib version, and always\n"
"\t\tenabled in tnc100em).\n"
"\t-S\tset scaling factor in range 1..4 (only has an effect in\n"
"\t\tgnc100em).\n"
"\n"
"  file_to_boot.bin	load file into RAM at 100h and run it. The file must\n"
"\t\t\tstart with F3h (`di'). (This is generally used to\n"
"\t\t\tboot ZCN (zcn.bin) when using the builtin `ROM'.)\n");
}


void parseoptions(int argc,char *argv[])
{
int done=0;

opterr=0;

do
  switch(getopt(argc,argv,"d25hmpr:sS:"))
    {
    case 'd':
      printf("running in debug mode...\n");
      breakpoint=1;
      break;
    case '2':
      nc200=1;
      nc150=0;
      break;
    case '5':
      nc150=1;
      nc200=0;
      break;
    case 'h':
      usage_help();
      exit(1);
    case 'm':
      alt_colour_msg=1;
      break;
    case 'p':	/* force PD ROM even if nc100.rom is present */
      force_pd_rom=1;
      break;
    case 'r':	/* refresh rate */
      scrn_freq=atoi(optarg);
      if(scrn_freq<2) scrn_freq=2;
      if(scrn_freq>100) scrn_freq=100;
      break;
    case 's':	/* enable serial emulation (only affects [gx]nc100em) */
      allow_serial=1;
      break;
    case 'S':	/* set scale factor */
      default_scale=atoi(optarg);
      if(default_scale<1) default_scale=1;
      if(default_scale>4) default_scale=4;
      break;
    case '?':
      switch(optopt)
        {
        case 'r':
          fprintf(stderr,"%s: "
          	"the -r option needs a refresh rate argument.\n",
                cmd_name);
          break;
        case 'S':
          fprintf(stderr,"%s: "
          	"the -S option needs a scaling factor argument.\n",
                cmd_name);
          break;
        default:
          fprintf(stderr,"%s: "
          	"option `%c' not recognised.\n",
                cmd_name,optopt);
        }
      exit(1);
    case -1:
      done=1;
    }
while(!done);

if(optind==argc-1)	/* if a filename given... */
  snprintf(binfile_name,sizeof(binfile_name),"%s",argv[optind]);

/* change scrnaddr if NC200 */
if(nc200) scrnaddr=0xe000;
}
