/* snc100em, an svgalib-based NC100 emulator for Linux.
 * Copyright (C) 1994 Ian Collier. nc100em changes (C) 1996,1999 Russell Marks.
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

/* This also compiles as a serial-only text version if TEXT_VER is
 * defined.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef TEXT_VER
#include <vga.h>
#include <vgagl.h>
#include <vgakeyboard.h>
#endif
#include "common.h"
#include "z80.h"


#ifdef TEXT_VER
char *cmd_name="tnc100em";
#else
char *cmd_name="snc100em";
#endif

int large_screen=0;
int large_rhs=0;
unsigned char ls_byte_bitmaps[512];


#define SCRN_XOFS	80		/* x offset of display */
#define SCRN_YOFS	(nc200?176:208)	/* y offset */

/* variants for zoomed-up mode (runs in 320x200) */
#define LARGE_XOFS	0
#define LARGE_YOFS	(nc200?36:68)


#ifdef TEXT_VER
struct autokey_tag
  {
  /* up to two keys to press this int */
  int port1,mask1;
  int port2,mask2;
  };

struct autokey_tag autokeys[]=
  {
  /* hold down both shifts for a while, to make sure we cold-boot */

  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},
  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},
  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},
  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},
  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},
  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},
  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},
  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},  {0,1, 0,2},
  
  /* now do |, s, enter. This should get a serial console going.
   * note that the speed/time we do this doesn't matter - ZCN will
   * buffer the keypresses anyway.
   */
  {0,1, 7,0x04},	/* | */
  {3,0x40, 0,0},	/* s */
  {0,0x10, 0,0},	/* enter */
  
  /* this stops the auto-keypressing */
  {0,0, 0,0}
  };
#endif



void dontpanic(int a)
{
#ifndef TEXT_VER
keyboard_close();
vga_setmode(TEXT);
#endif
serial_uninit();
writecard(cardmem);
exit(0);
}


void dontpanic_nmi()
{
#ifndef TEXT_VER
keyboard_close();
vga_setmode(TEXT);
#endif
serial_uninit();
#ifndef TEXT_VER	/* don't ever write RAM from tnc100em */
writeram(mem+RAM_START);
#endif
writecard(cardmem);
exit(0);
}


/* XXX not supported yet */
void get_mouse_state(int *mxp,int *myp,int *mbutp)
{
*mxp=*myp=*mbutp=0;
}


#ifndef TEXT_VER
void screenon(void)
{
static unsigned char buf[80];
int f;

if(large_screen)
  {
  vga_setmode(G320x200x256);
  vga_setpalette(0,63,63,63);
  vga_setpalette(1,0,0,0);
  }
else
  {
  vga_setmode(G640x480x2);
  memset(buf,255,sizeof(buf));
  for(f=0;f<480;f++)
    vga_drawscansegment(buf,0,f,sizeof(buf));
  }
}


void screenoff(void)
{
vga_setmode(TEXT);
}
#endif


#ifdef TEXT_VER
void press_autokeys(int pos)
{
keyports[autokeys[pos].port1]|=autokeys[pos].mask1;
keyports[autokeys[pos].port2]|=autokeys[pos].mask2;
}

void unpress_autokeys(void)
{
int f;

/* just unpress the lot */
for(f=0;f<10;f++) keyports[f]=0;
}
#endif


#ifndef TEXT_VER
/* redraw the screen */
void update_scrn(void)
{
static unsigned char oldscrn[60*128];
static unsigned char buf[60];
static int count=0;
int lines=(nc200?128:64);
unsigned char *ramptr,*oldptr;
int x,y,f;

/* only do it every 1/Nth */
count++;
if(count<scrn_freq) return; else count=0;

ramptr=mem+RAM_START+scrnaddr;
oldptr=oldscrn;

if(large_screen)
  {
  /* do 320x64/128 in 320x200 mode */
  unsigned char *scrnptr=graph_mem+LARGE_YOFS*320+LARGE_XOFS;
  int i,mask;
  
  if(large_rhs) ramptr+=20,oldptr+=20;
  
  for(y=0;y<lines*2;y+=2)
    {
    for(x=0;x<40*16;x+=16)
      {
      if(*oldptr!=*ramptr || force_full_redraw)
        for(mask=128,i=0;mask;i++,mask>>=1)
          *scrnptr++=(((*ramptr)&mask)?1:0);
      else
        scrnptr+=8;
      *oldptr++=*ramptr++;
      }
    ramptr+=24;
    oldptr+=20;
    }
  }
else
  {
  /* do 480x64/128 (original size) in 640x480 mode */
  for(y=0;y<lines;y++,ramptr+=64,oldptr+=60)
    if(memcmp(ramptr,oldptr,60)!=0 || force_full_redraw)
      {
      for(f=0;f<60;f++) buf[f]=~ramptr[f];
      vga_drawscansegment(buf,SCRN_XOFS,SCRN_YOFS+y,60);
      memcpy(oldptr,ramptr,60);
      }
  }

force_full_redraw=0;
}


void update_kybd(void)
{
int y;
char *keymap=keyboard_getstate();

#define UNPRESS(x) \
	while(keyboard_keypressed(x)) usleep(20000),keyboard_update();

keyboard_update();

if(keyboard_keypressed(SCANCODE_F8))
  {
  UNPRESS(SCANCODE_F8);
  dontpanic(0);	/* F8 = quit */
  /* XXX want this to merely issue NMI when it's all working...? */
  }

if(keyboard_keypressed(SCANCODE_F5))
  {
  UNPRESS(SCANCODE_F5);
  do_nmi=1;	/* F5 = NMI */
  }

if(keyboard_keypressed(SCANCODE_F10))
  {
  UNPRESS(SCANCODE_F10);
  do_nmi=1;	/* F10 also = NMI */
  }

if(keyboard_keypressed(SCANCODE_PAGEUP))
  {
  UNPRESS(SCANCODE_PAGEUP);
  large_rhs=0;
  if(!large_screen) large_screen=1,screenon();
  force_full_redraw=1;
  }

if(keyboard_keypressed(SCANCODE_PAGEDOWN))
  {
  UNPRESS(SCANCODE_PAGEDOWN);
  large_rhs=1;
  if(!large_screen) large_screen=1,screenon();
  force_full_redraw=1;
  }

if(keyboard_keypressed(SCANCODE_END))
  {
  UNPRESS(SCANCODE_END);
  if(large_screen) large_screen=0,screenon(),force_full_redraw=1;
  }

for(y=0;y<10;y++) keyports[y]=0;

/* this is a bit messy, but I don't think there's a nicer way. :-/ */
if(keymap[SCANCODE_ENTER])	keyports[0]|=0x10;
if(keymap[SCANCODE_CURSORBLOCKLEFT])	keyports[0]|=0x08;
if(keymap[SCANCODE_RIGHTSHIFT])	keyports[0]|=0x02;
if(keymap[SCANCODE_LEFTSHIFT])	keyports[0]|=0x01;
if(keymap[SCANCODE_5])	{if(nc200) keyports[2]|=0x10; else keyports[1]|=0x40;}
if(keymap[SCANCODE_SPACE])	keyports[1]|=0x08;
if(keymap[SCANCODE_ESCAPE])	keyports[1]|=0x04;
if(keymap[SCANCODE_LEFTCONTROL] || keymap[SCANCODE_RIGHTCONTROL])
	keyports[1]|=0x02;
if(keymap[SCANCODE_INSERT] || keymap[125])   /* 125 is left-win on W95 kybds */
	keyports[1]|=0x01; /* `function' */
if(keymap[SCANCODE_TAB])	keyports[2]|=0x08;
if(keymap[SCANCODE_1])		keyports[2]|=0x04;
if(keymap[SCANCODE_LEFTALT] || keymap[SCANCODE_RIGHTALT])
	keyports[2]|=0x02; /* `symbol' */
if(keymap[SCANCODE_CAPSLOCK])	keyports[2]|=0x01;
if(keymap[SCANCODE_D])		keyports[3]|=0x80;
if(keymap[SCANCODE_S])		keyports[3]|=0x40;
if(keymap[SCANCODE_E])		keyports[3]|=0x10;
if(keymap[SCANCODE_W])		keyports[3]|=0x08;
if(keymap[SCANCODE_Q])		keyports[3]|=0x04;
if(keymap[SCANCODE_2])		keyports[3]|=0x02;
if(keymap[SCANCODE_3])		keyports[3]|=0x01;
if(keymap[SCANCODE_F])		keyports[4]|=0x80;
if(keymap[SCANCODE_R])		keyports[4]|=0x40;
if(keymap[SCANCODE_A])		keyports[4]|=0x10;
if(keymap[SCANCODE_X])		keyports[4]|=0x08;
if(keymap[SCANCODE_Z])		keyports[4]|=0x04;
if(keymap[SCANCODE_4]) {if(nc200) keyports[0]|=0x04; else keyports[4]|=0x01;}
if(keymap[SCANCODE_C])		keyports[5]|=0x80;
if(keymap[SCANCODE_G])		keyports[5]|=0x40;
if(keymap[SCANCODE_Y])		keyports[5]|=0x20;
if(keymap[SCANCODE_T])		keyports[5]|=0x10;
if(keymap[SCANCODE_V])		keyports[5]|=0x08;
if(keymap[SCANCODE_B])		keyports[5]|=0x04;
if(keymap[SCANCODE_N])		keyports[6]|=0x80;
if(keymap[SCANCODE_H])		keyports[6]|=0x40;
if(keymap[SCANCODE_SLASH])	keyports[6]|=0x20;
if(keymap[SCANCODE_BACKSLASH])	keyports[6]|=0x10;	/* # on UK kybd */
if(keymap[SCANCODE_CURSORBLOCKRIGHT]) keyports[6]|=0x08;
if(keymap[SCANCODE_REMOVE])	keyports[6]|=0x04;	/* Delete */
if(keymap[SCANCODE_CURSORBLOCKDOWN])	keyports[6]|=0x02;
if(keymap[SCANCODE_6])	{if(nc200) keyports[2]|=0x40; else keyports[6]|=0x01;}
if(keymap[SCANCODE_K])		keyports[7]|=0x80;
if(keymap[SCANCODE_M])		keyports[7]|=0x40;
if(keymap[SCANCODE_U])		keyports[7]|=0x20;
if(keymap[SCANCODE_GRAVE])	keyports[7]|=0x10;
if(keymap[SCANCODE_CURSORBLOCKUP])	keyports[7]|=0x08;
if(keymap[SCANCODE_LESS])	keyports[7]|=0x04;	/* \ on UK kybd */
if(keymap[SCANCODE_7])	{if(nc200) keyports[4]|=0x02; else keyports[7]|=0x02;}
if(keymap[SCANCODE_EQUAL])
			{if(nc200) keyports[7]|=0x02; else keyports[7]|=0x01;}
if(keymap[SCANCODE_COMMA])	keyports[8]|=0x80;
if(keymap[SCANCODE_J])		keyports[8]|=0x40;
if(keymap[SCANCODE_I])		keyports[8]|=0x20;
if(keymap[SCANCODE_APOSTROPHE])	keyports[8]|=0x10;
if(keymap[SCANCODE_BRACKET_LEFT]) keyports[8]|=0x08;	/* [ */
if(keymap[SCANCODE_BRACKET_RIGHT]) keyports[8]|=0x04;	/* ] */
if(keymap[SCANCODE_MINUS])	keyports[8]|=0x02;
if(keymap[SCANCODE_8])	{if(nc200) keyports[4]|=0x01; else keyports[8]|=0x01;}
if(keymap[SCANCODE_PERIOD])	keyports[9]|=0x80;
if(keymap[SCANCODE_O])		keyports[9]|=0x40;
if(keymap[SCANCODE_L])		keyports[9]|=0x20;
if(keymap[SCANCODE_SEMICOLON])	keyports[9]|=0x10;
if(keymap[SCANCODE_P])		keyports[9]|=0x08;
if(keymap[SCANCODE_BACKSPACE])	keyports[9]|=0x04;
if(keymap[SCANCODE_9])	{if(nc200) keyports[1]|=0x80; else keyports[9]|=0x02;}
if(keymap[SCANCODE_0])	{if(nc200) keyports[9]|=0x02; else keyports[9]|=0x01;}
}
#endif		/* !TEXT_VER */


void do_interrupt(void)
{
static int scount=0;

#ifndef TEXT_VER
update_scrn();
update_kybd();
#else
static int autokey_pos=0;

if(autokey_pos>=0)
  {
  unpress_autokeys();
  autokey_pos++;
  if(autokeys[autokey_pos].mask1==0)
    autokey_pos=-1;
  else
    press_autokeys(autokey_pos);
  }
#endif

scount++;
if(scount==4) scount=0,serout_flush();
}


int main(int argc,char *argv[])
{
#ifndef TEXT_VER
vga_init();
#else
allow_serial=1;		/* always enabled, of course :-) */
press_autokeys(0);	/* so shifts are held down when we boot */
#endif

parseoptions(argc,argv);

common_init();		/* init memory etc. */

#ifndef TEXT_VER
screenon();
keyboard_init();
keyboard_translatekeys(DONT_CATCH_CTRLC);
#endif

startsigsandtimer();
mainloop();

/* not reached */
exit(0);
}
