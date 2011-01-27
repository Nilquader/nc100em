/* xnc100em, an Xlib-based NC100 emulator
 * 			(based on smain.c and xz80's xspectrum.c).
 * Copyright (C) 1994 Ian Collier. xnc100em changes (C) 1996 Russell Marks.
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
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Intrinsic.h>
#include <X11/Xatom.h>

#ifdef MITSHM
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#endif

#ifdef HAS_UNAME
#include <sys/utsname.h>
#endif

#include "common.h"
#include "z80.h"

#define MAX_DISP_LEN 256

#include "xnc100em.icon"

#if SCALE>1
static unsigned long scaleup[256]; /* to hold table of scaled up bytes,
                                 e.g. 00110100B -> 0000111100110000B */
#endif


#define BORDER_WIDTH	(5*SCALE)
int hsize=480*SCALE,vsize=64*SCALE;

int rrshm=10,rrnoshm=10,mitshm=1;


char *cmd_name="xnc100em";



/* prototypes for X stuff */
static int is_local_server(char *name);
static Display *open_display(int *argc,char **argv);
static int image_init(void);
static void notify(int *argc,char **argv);
void startup(int *argc,char **argv);
static int process_keypress(XKeyEvent *kev);
static void process_keyrelease(XKeyEvent *kev);
int check_events(int flag);
void refresh(void);
void closedown(void);




void dontpanic(int a)
{
closedown();
writecard(cardmem);
exit(0);
}


void dontpanic_nmi(void)
{
closedown();
writeram(mem+RAM_START);
writecard(cardmem);
exit(0);
}


/* XXX not supported yet */
void get_mouse_state(int *mxp,int *myp,int *mbutp)
{
*mxp=*myp=*mbutp=0;
}



int main(int argc,char *argv[])
{
parseoptions(argc,argv);

/* XXX */
if(nc200)
  {
  printf("NC200 emulation is not currently supported in the Xlib version.\n");
  printf("Try the GTK+ version (gnc100em) instead.\n");
  exit(1);
  }

common_init();

startup(&argc,argv);

startsigsandtimer();
mainloop();

/* not reached */
exit(0);
}


void do_interrupt(void)
{
static int count=0;
static int scount=0;

/* only do refresh() every 1/Nth */
count++;
if(count>=scrn_freq)
  count=0,refresh();
check_events(1);

scount++;
if(scount==4) scount=0,serout_flush();
}




/* the remainder of xmain.c is based on xz80's xspectrum.c. */

#ifdef SunKludge
char *shmat();
char *getenv();
#endif

int refresh_screen=1;

/* remember, this table is ignoring shifts... */
static struct {unsigned char port,mask;} keytable[]={
/*  SP      !        "        #        $        %        &        '  */
{1,0x08},{2,0x04},{3,0x01},{6,0x10},{4,0x01},{1,0x40},{7,0x02},{7,0x04},
/*  (       )        *        +        ,        -        .        /  */
{9,0x02},{9,0x01},{8,0x01},{7,0x01},{8,0x80},{8,0x02},{9,0x80},{6,0x20},
/*  0       1        2        3        4        5        6        7  */
{9,0x01},{2,0x04},{3,0x02},{3,0x01},{4,0x01},{1,0x40},{6,0x01},{7,0x02},
/*  8       9        :        ;        <        =        >        ?  */
{8,0x01},{9,0x02},{9,0x08},{9,0x08},{8,0x80},{7,0x01},{9,0x80},{6,0x20},
/*  @       A        B        C        D        E        F        G  */
{8,0x10},{4,0x10},{5,0x04},{5,0x80},{3,0x80},{3,0x10},{4,0x80},{5,0x40},
/*  H       I        J        K        L        M        N        O  */
{6,0x40},{8,0x20},{8,0x40},{7,0x80},{9,0x20},{7,0x40},{6,0x80},{9,0x40},
/*  P       Q        R        S        T        U        V        W  */
{9,0x08},{3,0x04},{4,0x40},{3,0x40},{5,0x10},{7,0x20},{5,0x08},{3,0x08},
/*  X       Y        Z        [        \        ]        ^        _  */
{4,0x08},{5,0x20},{4,0x04},{8,0x08},{7,0x04},{8,0x04},{6,0x01},{8,0x02},
/*  `       a        b        c        d        e        f        g  */
{7,0x10},{4,0x10},{5,0x04},{5,0x80},{3,0x80},{3,0x10},{4,0x80},{5,0x40},
/*  h       i        j        k        l        m        n        o  */
{6,0x40},{8,0x20},{8,0x40},{7,0x80},{9,0x20},{7,0x40},{6,0x80},{9,0x40},
/*  p       q        r        s        t        u        v        w  */
{9,0x08},{3,0x04},{4,0x40},{3,0x40},{5,0x10},{7,0x20},{5,0x08},{3,0x08},
/*  x       y        z        {        |        }        ~       DEL */
{4,0x08},{5,0x20},{4,0x04},{8,0x08},{7,0x04},{8,0x04},{6,0x10},{9,0x04}
};


static Display *display;
static Screen *scrptr;
static int screen;
static Window root;
static Colormap cmap;
static GC maingc;
static GC fggc, bggc;
static Window borderwin, mainwin;
static XImage *ximage;
static unsigned char *image;
static int linelen;
static int black,white;
#ifdef MITSHM
static XShmSegmentInfo xshminfo;
#endif
static int invert=0;
static int borderchange=1;

/* needed to catch wm delete-window message */
static Atom proto_atom=None,delete_atom=None;
static int quitting_already=0;


static int is_local_server(char *name)
{
#ifdef HAS_UNAME
   struct utsname un;
#else
   char sysname[MAX_DISP_LEN];
#endif

   if(name[0]==':')return 1;
   if(!strncmp(name,"unix",4))return 1;
   if(!strncmp(name,"localhost",9))return 1;

#ifdef HAS_UNAME
   uname(&un);
   if(!strncmp(name,un.sysname,strlen(un.sysname)))return 1;
   if(!strncmp(name,un.nodename,strlen(un.nodename)))return 1;
#else
   gethostname(sysname,MAX_DISP_LEN);
   if(!strncmp(name,sysname,strlen(sysname)))return 1;
#endif
   return 0;
}


static Display *open_display(int *argc,char **argv)
{
   char *ptr;
   char dispname[MAX_DISP_LEN];
   Display *display;

   if((ptr=getenv("DISPLAY")))
     strcpy(dispname,ptr);
   else
     strcpy(dispname,":0.0");
   
   if(!(display=XOpenDisplay(dispname))){
      fprintf(stderr,"Unable to open display %s\n",dispname);
      exit(1);
   }

#ifdef MITSHM   
   mitshm=1;
#else
   mitshm=0;
#endif
   
   /* XXX deal with args here */

   if(mitshm && !is_local_server(dispname)){
      fputs("Disabling MIT-SHM on remote X server\n",stderr);
      mitshm=0;
   }
   return display;
}


static int image_init(void)
{
#ifdef MITSHM
   if(mitshm){
      ximage=XShmCreateImage(display,DefaultVisual(display,screen),
             DefaultDepth(display,screen),ZPixmap,NULL,&xshminfo,
             hsize,vsize);
      if(!ximage){
         fputs("Couldn't create X image\n",stderr);
         return 1;
      }
      xshminfo.shmid=shmget(IPC_PRIVATE,
               ximage->bytes_per_line*(ximage->height+1),IPC_CREAT|0777);
      if(xshminfo.shmid == -1){
         perror("Couldn't perform shmget");
         return 1;
      }
      xshminfo.shmaddr=ximage->data=shmat(xshminfo.shmid,0,0);
      if(!xshminfo.shmaddr){
         perror("Couldn't perform shmat");
         return 1;
      }
      xshminfo.readOnly=0;
      if(!XShmAttach(display,&xshminfo)){
         perror("Couldn't perform XShmAttach");
         return 1;
      }
      scrn_freq=rrshm;
   } else
#endif
   {
      ximage=XCreateImage(display,DefaultVisual(display,screen),
             DefaultDepth(display,screen),ZPixmap,0,NULL,hsize,vsize,
             8,0);
      if(!ximage){
         perror("XCreateImage failed");
         return 1;
      }
      ximage->data=malloc(ximage->bytes_per_line*(ximage->height+1));
      if(!ximage->data){
         perror("Couldn't get memory for XImage data");
         return 1;
      }
      scrn_freq=rrnoshm;
   }
   linelen=ximage->bytes_per_line/SCALE;
#if 0
   if(linelen==60 &&
         (BitmapBitOrder(display)!=MSBFirst || ImageByteOrder(display)!=MSBFirst))
      fprintf(stderr,"BitmapBitOrder=%s and ImageByteOrder=%s.\n",
         BitmapBitOrder(display)==MSBFirst?"MSBFirst":"LSBFirst",
         ImageByteOrder(display)==MSBFirst?"MSBFirst":"LSBFirst"),
      fputs("If the display is mixed up, please mail me these values\n",stderr),
      fputs("and describe the display as accurately as possible.\n",stderr);
#endif
   ximage->data+=linelen;
   image=ximage->data;
   return 0;
}


static void notify(int *argc,char **argv)
{
   Pixmap icon;
   XWMHints xwmh;
   XSizeHints xsh;
   XClassHint xch;
   XTextProperty appname, iconname;
   char *apptext;
   char *icontext="xnc100em";

   icon=XCreatePixmapFromBitmapData(display,root,xnc100em_bits,
        xnc100em_width,xnc100em_height,black,white,
        DefaultDepth(display,screen));
   apptext="xnc100em";
   xsh.flags=PSize|PMinSize|PMaxSize;
   xsh.min_width=hsize;
   xsh.min_height=vsize;
   xsh.max_width=hsize+BORDER_WIDTH*2;
   xsh.max_height=vsize+BORDER_WIDTH*2;
   if(!XStringListToTextProperty(&apptext,1,&appname)){
      fputs("Can't create a TextProperty!",stderr);
      return;
   }
   if(!XStringListToTextProperty(&icontext,1,&iconname)){
      fputs("Can't create a TextProperty!",stderr);
      return;
   }
   xwmh.initial_state=NormalState;
   xwmh.input=1;
   xwmh.icon_pixmap=icon;
   xwmh.flags=StateHint|IconPixmapHint|InputHint;
   xch.res_name="xnc100em";
   xch.res_class="Xnc100em";
   XSetWMProperties(display,borderwin,&appname,&iconname,argv,
      *argc,&xsh,&xwmh,&xch);

   /* delete-window message stuff, from xloadimage via dosemu. */
   proto_atom =XInternAtom(display,"WM_PROTOCOLS",False);
   delete_atom=XInternAtom(display,"WM_DELETE_WINDOW",False);
   if(proto_atom!=None && delete_atom!=None)
   XChangeProperty(display,borderwin,proto_atom,XA_ATOM,32,
   		PropModePrepend,(char *)&delete_atom,1);
}


#if SCALE>1
static void scaleup_init(void)
{
   int j, k, l, m;
   unsigned long bigval; /* SCALING must be <= 4! */
   for(l=0;l<256;scaleup[l++]=bigval)
      for(m=l,bigval=j=0;j<8;j++) {
         for(k=0;k<SCALE;k++)
            bigval=(bigval>>1)|((m&1)<<31);
         m>>=1;
      }
}
#endif


void startup(int *argc,char **argv)
{
   display=open_display(argc,argv);
   if(!display){
      fputs("Failed to open X display\n",stderr);
      exit(1);
   }
   invert=0;
   screen=DefaultScreen(display);
   scrptr=DefaultScreenOfDisplay(display);
   root=DefaultRootWindow(display);
   white=WhitePixel(display,screen);
   black=BlackPixel(display,screen);
   maingc=XCreateGC(display,root,0,NULL);
   XCopyGC(display,DefaultGC(display,screen),~0,maingc);
   XSetGraphicsExposures(display,maingc,0);
   fggc=XCreateGC(display,root,0,NULL);
   XCopyGC(display,DefaultGC(display,screen),~0,fggc);
   XSetGraphicsExposures(display,fggc,0);
   bggc=XCreateGC(display,root,0,NULL);
   XCopyGC(display,DefaultGC(display,screen),~0,bggc);
   XSetGraphicsExposures(display,bggc,0);
   cmap=DefaultColormap(display,screen);
   if(image_init()){
      if(mitshm){
         fputs("Failed to create X image - trying again with mitshm off\n",stderr);
         mitshm=0;
         if(image_init())exit(1);
      }
      else exit(1);
   }

#if SCALE>1
   if(linelen==60) scaleup_init();
#endif

   borderwin=XCreateSimpleWindow(display,root,0,0,
             hsize+BORDER_WIDTH*2,vsize+BORDER_WIDTH*2,0,0,0);
   mainwin=XCreateSimpleWindow(display,borderwin,BORDER_WIDTH,
             BORDER_WIDTH,hsize,vsize,0,0,0);
   notify(argc,argv);
   XSelectInput(display,borderwin,KeyPressMask|KeyReleaseMask|
      ExposureMask|EnterWindowMask|LeaveWindowMask|
      StructureNotifyMask);
   XSelectInput(display,mainwin,ExposureMask);
   XMapRaised(display,borderwin);
   XMapRaised(display,mainwin);
   XFlush(display);
   refresh_screen=1;
}


static int process_keypress(XKeyEvent *kev)
{
   char buf[3];
   KeySym ks;

   XLookupString(kev,buf,2,&ks,NULL);

   switch(ks){
      case XK_F5:
      case XK_F10:
        do_nmi=1;
        break;
      case XK_F8:
        dontpanic(0);
        /* doesn't return */
        break;
      
      /* Insert = Function, but allow super/hyper to act as Function too. */
      case XK_Insert:
      case XK_Super_L: case XK_Super_R:
      case XK_Hyper_L: case XK_Hyper_R:
              keyports[1]|=0x01; break;
      case XK_Return: 
              keyports[0]|=0x10; break;
      case XK_Control_L:
              keyports[1]|=0x02; break;
      case XK_Shift_L:
              keyports[0]|=0x01; break;
      case XK_Shift_R:
              keyports[0]|=0x02; break;
      case XK_Alt_L: case XK_Alt_R:
      case XK_Meta_L: case XK_Meta_R:
              keyports[2]|=0x02;
              break;
      case XK_Caps_Lock:
              keyports[2]|=0x01; break;
      case XK_BackSpace: case XK_Delete:
              keyports[9]|=0x04; break;
      case XK_Escape:
              keyports[1]|=0x04; break;
      case XK_Tab:
              keyports[2]|=0x08; break;
      case XK_Up:
              keyports[7]|=0x08; break;
      case XK_Down:
              keyports[6]|=0x02; break;
      case XK_Left:
              keyports[0]|=0x08; break;
      case XK_Right:
              keyports[6]|=0x08; break;
      case XK_minus:
              keyports[8]|=0x02; break;
      case XK_underscore:
              keyports[8]|=0x02; break;
      case XK_equal:
              keyports[7]|=0x01; break;
      case XK_plus:
              keyports[7]|=0x01; break;
      case XK_semicolon:
              keyports[9]|=0x10; break;
      case XK_colon:
              keyports[9]|=0x10; break;
      case XK_apostrophe:
              keyports[8]|=0x10; break;
      case XK_quotedbl:
              keyports[3]|=0x02; break;
      case XK_exclam:
              keyports[2]|=0x04; break;
      case XK_at:
              keyports[8]|=0x10; break;
      case XK_numbersign:
              keyports[6]|=0x10; break;
      case XK_sterling:
              keyports[3]|=0x01; break;
      case XK_dollar:
              keyports[4]|=0x01; break;
      case XK_percent:
              keyports[1]|=0x40; break;
      case XK_asciicircum:
              keyports[6]|=0x01; break;
      case XK_ampersand:
              keyports[7]|=0x02; break;
      case XK_asterisk:
              keyports[8]|=0x01; break;
      case XK_parenleft:
              keyports[9]|=0x02; break;
      case XK_parenright:
              keyports[9]|=0x01; break;
      case XK_comma:
              keyports[8]|=0x80; break;
      case XK_less:
              keyports[8]|=0x80; break;
      case XK_period:
              keyports[9]|=0x80; break;
      case XK_greater:
              keyports[9]|=0x80; break;
      case XK_slash:
              keyports[6]|=0x20; break;
      case XK_question:
              keyports[6]|=0x20; break;
      case XK_backslash: case XK_bar:
              keyports[7]|=0x04; break;
              break;
      default:
              if((int)(ks-=32)>=0 && ks<128)
                 keyports[keytable[ks].port]|=keytable[ks].mask;
   }
   return 0;
}


#undef dosym
#define dosym (symbolshift || (keyports[7]|=2))

static void process_keyrelease(XKeyEvent *kev)
{
   char buf[3];
   KeySym ks;

   XLookupString(kev,buf,2,&ks,NULL);

   switch(ks){
      case XK_Insert:
      case XK_Super_L: case XK_Super_R:
      case XK_Hyper_L: case XK_Hyper_R:
              keyports[1]&=~0x01; break;

      case XK_Return: 
              keyports[0]&=~0x10; break;
      case XK_Control_L:
              keyports[1]&=~0x02; break;
      case XK_Shift_L:
              keyports[0]&=~0x01; break;
      case XK_Shift_R:
              keyports[0]&=~0x02; break;
      case XK_Alt_L: case XK_Alt_R:
      case XK_Meta_L: case XK_Meta_R:
              keyports[2]&=~0x02;
              break;
      case XK_Caps_Lock:
              keyports[2]&=~0x01; break;
      case XK_BackSpace: case XK_Delete:
              keyports[9]&=~0x04; break;
      case XK_Escape:
              keyports[1]&=~0x04; break;
      case XK_Tab:
              keyports[2]&=~0x08; break;
      case XK_Up:
              keyports[7]&=~0x08; break;
      case XK_Down:
              keyports[6]&=~0x02; break;
      case XK_Left:
              keyports[0]&=~0x08; break;
      case XK_Right:
              keyports[6]&=~0x08; break;
      case XK_minus:
              keyports[8]&=~0x02; break;
      case XK_underscore:
              keyports[8]&=~0x02; break;
      case XK_equal:
              keyports[7]&=~0x01; break;
      case XK_plus:
              keyports[7]&=~0x01; break;
      case XK_semicolon:
              keyports[9]&=~0x10; break;
      case XK_colon:
              keyports[9]&=~0x10; break;
      case XK_apostrophe:
              keyports[8]&=~0x10; break;
      case XK_quotedbl:
              keyports[3]&=~0x02; break;
      case XK_exclam:
              keyports[2]&=~0x04; break;
      case XK_at:
              keyports[8]&=~0x10; break;
      case XK_numbersign:
              keyports[6]&=~0x10; break;
      case XK_sterling:
              keyports[3]&=~0x01; break;
      case XK_dollar:
              keyports[4]&=~0x01; break;
      case XK_percent:
              keyports[1]&=~0x40; break;
      case XK_asciicircum:
              keyports[6]&=~0x01; break;
      case XK_ampersand:
              keyports[7]&=~0x02; break;
      case XK_asterisk:
              keyports[8]&=~0x01; break;
      case XK_parenleft:
              keyports[9]&=~0x02; break;
      case XK_parenright:
              keyports[9]&=~0x01; break;
      case XK_comma:
              keyports[8]&=~0x80; break;
      case XK_less:
              keyports[8]&=~0x80; break;
      case XK_period:
              keyports[9]&=~0x80; break;
      case XK_greater:
              keyports[9]&=~0x80; break;
      case XK_slash:
              keyports[6]&=~0x20; break;
      case XK_question:
              keyports[6]&=~0x20; break;
      case XK_backslash: case XK_bar:
              keyports[7]&=~0x04; break;
              break;
      default:
              if((int)(ks-=32)>=0 && ks<96)
                 keyports[keytable[ks].port]&=~keytable[ks].mask;
   }
   return;
}


int check_events(int flag)
{
   static int answer=0;
   int i;
   static XEvent xev;
   XConfigureEvent *conf_ev;
   XCrossingEvent *cev;
   
   while (XEventsQueued(display,QueuedAfterReading)){
      XNextEvent(display,&xev);
      switch(xev.type){
         case Expose: refresh_screen=1;
            break;
         case ConfigureNotify:
            conf_ev=(XConfigureEvent *)&xev;
            XMoveWindow(display,mainwin,(conf_ev->width-hsize)/2,
                        (conf_ev->height-vsize)/2);
            break;
         case MapNotify: case UnmapNotify: case ReparentNotify:
            break;
         case EnterNotify:
            cev=(XCrossingEvent *)&xev;
            if(cev->detail!=NotifyInferior)
               XAutoRepeatOff(display),XFlush(display);
            break;
         case LeaveNotify:
            cev=(XCrossingEvent *)&xev;
            if(cev->detail!=NotifyInferior)
               XAutoRepeatOn(display),XFlush(display);
            break;
         case KeyPress: i=process_keypress((XKeyEvent *)&xev);
            if(!answer)answer=i;
            break;
         case KeyRelease: process_keyrelease((XKeyEvent *)&xev);
            break;
         case ClientMessage:
            if(!quitting_already && xev.xclient.data.l[0]==delete_atom)
              {
              /* got delete message from wm, quit */
              quitting_already=1;
              do_nmi=1;
              }
            break;
         default:
            fprintf(stderr,"unhandled X event, type %d\n",xev.type);
      }
   }
   if(flag){
      i=answer;
      answer=0;
      return i;
   }
   else return 0;
}


void refresh(void)
{
   static unsigned char oldscrn[60*64];
   unsigned char *ramptr,*oldptr;
   int i,x,y;
   unsigned char *src,*dst;
   unsigned char val;
   int mask;
   int ymin,ymax;
#if SCALE>=2
   int m,j,k;
#endif
   
   if(borderchange>0)
     {
     /* XXX what about expose events? need to set borderchange... */
     XSetWindowBackground(display,borderwin,white);
     XClearWindow(display,borderwin);
     XFlush(display);
     borderchange=0;
     }

   ramptr=mem+RAM_START+scrnaddr;
   oldptr=oldscrn;
   ymin=64; ymax=-1;

   src=mem+RAM_START+scrnaddr;
   dst=image;
   for(y=0;y<64;y++,src+=4,ramptr+=64,oldptr+=60)
     if(memcmp(ramptr,oldptr,60)==0 && !force_full_redraw)
       {
       src+=60;
       dst+=linelen*SCALE*SCALE;
       }
     else
       {
       memcpy(oldptr,ramptr,60);
       if(y<ymin) ymin=y;
       if(y>ymax) ymax=y;
       
       switch(linelen)
         {
         case 60:
           /* XXX need to do scaleup for mono */
           /* 1-bit mono */
           for(x=0;x<60;x++,src++)
             *dst++=~(*src);
           break;
         
         case 480:
           /* 8-bit */
           for(x=0;x<60;x++,src++)
             for(i=0,val=(*src),mask=128;i<8;i++,mask>>=1)
#if SCALE<2
               /* i.e. actual size */
               *dst++=((val&mask)?black:white);
#else
               {
               m=((val&mask)?black:white);
               for(j=0;j<SCALE;j++)
                 for(k=0;k<SCALE;k++)
                   dst[j*hsize+k]=m;
               dst+=SCALE;
               }
           dst+=(SCALE-1)*hsize;
#endif
           break;
         
         default:
           /* otherwise, use generic approach (a bit slow, but should work
            * for everything).
            */
           for(x=0;x<480;x+=8,src++)
             for(i=0,val=(*src),mask=128;i<8;i++,mask>>=1)
#if SCALE<2
               /* i.e. actual size */
               XPutPixel(ximage,x+i,y,(val&mask)?black:white);
#else
               {
               /* tricky - would rectangles work out quicker or not? */
               m=((val&mask)?black:white);
               for(j=0;j<SCALE;j++)
                 for(k=0;k<SCALE;k++)
                   XPutPixel(ximage,(x+i)*SCALE+k,y*SCALE+j,
                   	(val&mask)?black:white);
               }
#endif
           break;
         }
       }
   
   if(refresh_screen || force_full_redraw)
     {
     ymin=0; ymax=63;
     force_full_redraw=refresh_screen=0;
     }
   
   if(ymax>=ymin)
     {
#ifdef MITSHM
     if(mitshm)
       XShmPutImage(display,mainwin,maingc,ximage,0,ymin*SCALE,0,ymin*SCALE,
       		hsize,(ymax-ymin+1)*SCALE,0);
     else
#endif
       XPutImage(display,mainwin,maingc,ximage,0,ymin*SCALE,0,ymin*SCALE,
       		hsize,(ymax-ymin+1)*SCALE);
     XFlush(display);
     }
}


void closedown(void)
{
#ifdef MITSHM
   if(mitshm){
      XShmDetach(display,&xshminfo);
      XDestroyImage(ximage);
      shmdt(xshminfo.shmaddr);
      shmctl(xshminfo.shmid,IPC_RMID,0);
   }
#endif
   XAutoRepeatOn(display);
   XCloseDisplay(display);
   serial_uninit();
}
