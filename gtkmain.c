/* gnc100em, a GTK+-based NC100 emulator for X.
 * Copyright (C) 1999 Russell Marks.
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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "common.h"
#include "z80.h"

#include "nc100.xpm"


/* GTK+ border width in scrolled window (not counting scrollbars).
 * very kludgey, but needed for calculating size to fit scrolly win to.
 */
#define SW_BORDER_WIDTH		4

/* since the window can get big horizontally, we limit the max width
 * to screen_width minus this.
 */
#define HORIZ_MARGIN_WIDTH	20

/* width of the border (on all sides) around the actual screen image. */
#define BORDER_WIDTH		5


char *cmd_name="gnc100em";

int scale;
int hsize,vsize;

GtkWidget *drawing_area;

/* image used for drawing area (faster than a pixmap :-)) */
GdkImage *image=NULL;

GtkWidget *window;
GtkWidget *scrolled_window;
GtkWidget *sdown_button,*sup_button;

gulong blackpix,whitepix;

int need_keyrep_restore=0;


/* size of minimum rectangle of image which needs to be redrawn. */
struct update_area
  {
  int xmin,xmax;
  int ymin,ymax;
  };


void closedown(void)
{
/* image may well be using shared memory, so it's probably a Good Idea
 * to blast it :-)
 */
gdk_image_destroy(image);

if(need_keyrep_restore)
  gdk_key_repeat_restore();

serial_uninit();
}


void dontpanic(int a)
{
closedown();
writecard(cardmem);
gtk_exit(0);
}


void dontpanic_nmi(void)
{
closedown();
writeram(mem+RAM_START);
writecard(cardmem);
gtk_exit(0);
}


/* get mouse state. x/y should not be overwritten if pointer is not
 * in window. buttons returned as *mbutp are l=1,m=2,r=4.
 */
void get_mouse_state(int *mxp,int *myp,int *mbutp)
{
GdkModifierType mod;
int mx,my;

gdk_window_get_pointer(drawing_area->window,&mx,&my,&mod);

if(mx<BORDER_WIDTH*scale || mx>=BORDER_WIDTH*scale+hsize ||
   my<BORDER_WIDTH*scale || my>=BORDER_WIDTH*scale+vsize)
  {
  *mbutp=0;
  return;
  }

*mxp=mx/scale-BORDER_WIDTH;
*myp=my/scale-BORDER_WIDTH;
*mbutp=(((mod&GDK_BUTTON1_MASK)?1:0)|
        ((mod&GDK_BUTTON2_MASK)?2:0)|
        ((mod&GDK_BUTTON3_MASK)?4:0));
}    


void destroy(GtkWidget *widget, gpointer data)
{
do_nmi=1;
}


static gint expose_event(GtkWidget *widget,GdkEventExpose *event)
{
int x,y,w,h;	/* x,y,w,h in image (which doesn't have border) */
int dx,dy;	/* dest x,y of bit of image we draw */
int rx,ry,rw,rh;	/* x,y,w,h of rectangle drawn before image */
int ox,oy;	/* top-left of border (not image) in window */
int bw=BORDER_WIDTH*scale;

ox=(widget->allocation.width-(hsize+bw*2))/2;
oy=(widget->allocation.height-(vsize+bw*2))/2;

x=dx=event->area.x;
y=dy=event->area.y;
w=event->area.width;
h=event->area.height;

x-=bw+ox;
y-=bw+oy;
/* w/h will need to be reduced if origin is off to top or left of image */
if(x<0) w+=x,dx-=x,x=0;
if(y<0) h+=y,dy-=y,y=0;
/* ...or off bottom or right. */
if(x+w>hsize) w=hsize-x;
if(y+h>vsize) h=vsize-y;

/* similar stuff for border rectangle */
rx=event->area.x;
ry=event->area.y;
rw=event->area.width;
rh=event->area.height;

if(rx<ox) rw-=ox-rx,rx=ox;
if(ry<oy) rh-=oy-ry,ry=oy;
if(rx+rw>ox+hsize+bw*2) rw=ox+hsize+bw*2-rx;
if(ry+rh>oy+vsize+bw*2) rh=oy+vsize+bw*2-ry;

/* white out the drawing area first (to draw any borders) */
if(rw>0 && rh>0)
  {
  gdk_draw_rectangle(widget->window,drawing_area->style->white_gc,TRUE,
  	rx,ry,rw,rh);
  }

/* now draw part of image over that if needed */
if(w>0 && h>0)
  {
  gdk_draw_image(widget->window,
  	widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
  	image,
  	x,y,dx,dy,w,h);
  }

return(FALSE);
}



struct keytable_tag
  {
  unsigned char port,mask;
  };


/* remember, this table is ignoring shifts... */
static struct keytable_tag nc100_keytable[]={
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

/* subtle but important differences on the '200... */
static struct keytable_tag nc200_keytable[]={
/*  SP      !        "        #        $        %        &        '  */
{1,0x08},{2,0x04},{3,0x01},{6,0x10},{0,0x04},{2,0x10},{4,0x02},{7,0x04},
/*  (       )        *        +        ,        -        .        /  */
{1,0x80},{9,0x02},{4,0x01},{7,0x02},{8,0x80},{8,0x02},{9,0x80},{6,0x20},
/*  0       1        2        3        4        5        6        7  */
{9,0x02},{2,0x04},{3,0x02},{3,0x01},{0,0x04},{2,0x10},{2,0x40},{4,0x02},
/*  8       9        :        ;        <        =        >        ?  */
{4,0x01},{1,0x80},{9,0x08},{9,0x08},{8,0x80},{7,0x02},{9,0x80},{6,0x20},
/*  @       A        B        C        D        E        F        G  */
{8,0x10},{4,0x10},{5,0x04},{5,0x80},{3,0x80},{3,0x10},{4,0x80},{5,0x40},
/*  H       I        J        K        L        M        N        O  */
{6,0x40},{8,0x20},{8,0x40},{7,0x80},{9,0x20},{7,0x40},{6,0x80},{9,0x40},
/*  P       Q        R        S        T        U        V        W  */
{9,0x08},{3,0x04},{4,0x40},{3,0x40},{5,0x10},{7,0x20},{5,0x08},{3,0x08},
/*  X       Y        Z        [        \        ]        ^        _  */
{4,0x08},{5,0x20},{4,0x04},{8,0x08},{7,0x04},{8,0x04},{2,0x40},{8,0x02},
/*  `       a        b        c        d        e        f        g  */
{7,0x10},{4,0x10},{5,0x04},{5,0x80},{3,0x80},{3,0x10},{4,0x80},{5,0x40},
/*  h       i        j        k        l        m        n        o  */
{6,0x40},{8,0x20},{8,0x40},{7,0x80},{9,0x20},{7,0x40},{6,0x80},{9,0x40},
/*  p       q        r        s        t        u        v        w  */
{9,0x08},{3,0x04},{4,0x40},{3,0x40},{5,0x10},{7,0x20},{5,0x08},{3,0x08},
/*  x       y        z        {        |        }        ~       DEL */
{4,0x08},{5,0x20},{4,0x04},{8,0x08},{7,0x04},{8,0x04},{6,0x10},{9,0x04}
};


static struct keytable_tag *keytable=nc100_keytable;


static gint key_press_event(GtkWidget *widget,GdkEventKey *event)
{
switch(event->keyval)
  {
  case GDK_F5:
  case GDK_F10:
    do_nmi=1;
    break;
  case GDK_F8:
    dontpanic(0);
    /* doesn't return */
    break;
  
  /* Insert = Function, but allow super/hyper to act as Function too. */
  case GDK_Insert:
  case GDK_Super_L: case GDK_Super_R:
  case GDK_Hyper_L: case GDK_Hyper_R:
    keyports[1]|=0x01; break;

  case GDK_Return: 
    keyports[0]|=0x10; break;
  case GDK_Control_L:
    keyports[1]|=0x02; break;
  case GDK_Shift_L:
    keyports[0]|=0x01; break;
  case GDK_Shift_R:
    keyports[0]|=0x02; break;
  case GDK_Alt_L: case GDK_Alt_R:
  case GDK_Meta_L: case GDK_Meta_R:
    keyports[2]|=0x02; break;
  case GDK_Caps_Lock:
    keyports[2]|=0x01; break;
  case GDK_BackSpace: case GDK_Delete:
    keyports[9]|=0x04; break;
  case GDK_Escape:
    keyports[1]|=0x04; break;
  case GDK_Tab:
    keyports[2]|=0x08; break;
  case GDK_Up:
    keyports[7]|=0x08; break;
  case GDK_Down:
    keyports[6]|=0x02; break;
  case GDK_Left:
    keyports[0]|=0x08; break;
  case GDK_Right:
    keyports[6]|=0x08; break;
  case GDK_minus:
    keyports[8]|=0x02; break;
  case GDK_underscore:
    keyports[8]|=0x02; break;
  case GDK_equal:
    if(nc200) keyports[7]|=0x02; else keyports[7]|=0x01; break;
  case GDK_plus:
    if(nc200) keyports[7]|=0x02; else keyports[7]|=0x01; break;
  case GDK_semicolon:
    keyports[9]|=0x10; break;
  case GDK_colon:
    keyports[9]|=0x10; break;
  case GDK_apostrophe:
    keyports[8]|=0x10; break;
  case GDK_quotedbl:
    keyports[3]|=0x02; break;
  case GDK_exclam:
    keyports[2]|=0x04; break;
  case GDK_at:
    keyports[8]|=0x10; break;
  case GDK_numbersign:
    keyports[6]|=0x10; break;
  case GDK_sterling:
    keyports[3]|=0x01; break;
  case GDK_dollar:
    if(nc200) keyports[0]|=0x04; else keyports[4]|=0x01; break;
  case GDK_percent:
    if(nc200) keyports[2]|=0x10; else keyports[1]|=0x40; break;
  case GDK_asciicircum:
    if(nc200) keyports[2]|=0x40; else keyports[6]|=0x01; break;
  case GDK_ampersand:
    if(nc200) keyports[4]|=0x02; else keyports[7]|=0x02; break;
  case GDK_asterisk:
    if(nc200) keyports[4]|=0x01; else keyports[8]|=0x01; break;
  case GDK_parenleft:
    if(nc200) keyports[1]|=0x80; else keyports[9]|=0x02; break;
  case GDK_parenright:
    if(nc200) keyports[9]|=0x02; else keyports[9]|=0x01; break;
  case GDK_comma:
    keyports[8]|=0x80; break;
  case GDK_less:
    keyports[8]|=0x80; break;
  case GDK_period:
    keyports[9]|=0x80; break;
  case GDK_greater:
    keyports[9]|=0x80; break;
  case GDK_slash:
    keyports[6]|=0x20; break;
  case GDK_question:
    keyports[6]|=0x20; break;
  case GDK_backslash: case GDK_bar:
    keyports[7]|=0x04; break;
  
  default:
    if(event->keyval>=32 && event->keyval<128)
      keyports[keytable[event->keyval-32].port]|=
      		keytable[event->keyval-32].mask;
  }

return(TRUE);
}


static gint key_release_event(GtkWidget *widget,GdkEventKey *event)
{
switch(event->keyval)
  {
  case GDK_Insert:
  case GDK_Super_L: case GDK_Super_R:
  case GDK_Hyper_L: case GDK_Hyper_R:
    keyports[1]&=~0x01; break;

  case GDK_Return: 
    keyports[0]&=~0x10; break;
  case GDK_Control_L:
    keyports[1]&=~0x02; break;
  case GDK_Shift_L:
    keyports[0]&=~0x01; break;
  case GDK_Shift_R:
    keyports[0]&=~0x02; break;
  case GDK_Alt_L: case GDK_Alt_R:
  case GDK_Meta_L: case GDK_Meta_R:
    keyports[2]&=~0x02; break;
  case GDK_Caps_Lock:
    keyports[2]&=~0x01; break;
  case GDK_BackSpace: case GDK_Delete:
    keyports[9]&=~0x04; break;
  case GDK_Escape:
    keyports[1]&=~0x04; break;
  case GDK_Tab:
    keyports[2]&=~0x08; break;
  case GDK_Up:
    keyports[7]&=~0x08; break;
  case GDK_Down:
    keyports[6]&=~0x02; break;
  case GDK_Left:
    keyports[0]&=~0x08; break;
  case GDK_Right:
    keyports[6]&=~0x08; break;
  case GDK_minus:
    keyports[8]&=~0x02; break;
  case GDK_underscore:
    keyports[8]&=~0x02; break;
  case GDK_equal:
    if(nc200) keyports[7]&=~0x02; else keyports[7]&=~0x01; break;
  case GDK_plus:
    if(nc200) keyports[7]&=~0x02; else keyports[7]&=~0x01; break;
  case GDK_semicolon:
    keyports[9]&=~0x10; break;
  case GDK_colon:
    keyports[9]&=~0x10; break;
  case GDK_apostrophe:
    keyports[8]&=~0x10; break;
  case GDK_quotedbl:
    keyports[3]&=~0x02; break;
  case GDK_exclam:
    keyports[2]&=~0x04; break;
  case GDK_at:
    keyports[8]&=~0x10; break;
  case GDK_numbersign:
    keyports[6]&=~0x10; break;
  case GDK_sterling:
    keyports[3]&=~0x01; break;
  case GDK_dollar:
    if(nc200) keyports[0]&=~0x04; else keyports[4]&=~0x01; break;
  case GDK_percent:
    if(nc200) keyports[2]&=~0x10; else keyports[1]&=~0x40; break;
  case GDK_asciicircum:
    if(nc200) keyports[2]&=~0x40; else keyports[6]&=~0x01; break;
  case GDK_ampersand:
    if(nc200) keyports[4]&=~0x02; else keyports[7]&=~0x02; break;
  case GDK_asterisk:
    if(nc200) keyports[4]&=~0x01; else keyports[8]&=~0x01; break;
  case GDK_parenleft:
    if(nc200) keyports[1]&=~0x80; else keyports[9]&=~0x02; break;
  case GDK_parenright:
    if(nc200) keyports[9]&=~0x02; else keyports[9]&=~0x01; break;
  case GDK_comma:
    keyports[8]&=~0x80; break;
  case GDK_less:
    keyports[8]&=~0x80; break;
  case GDK_period:
    keyports[9]&=~0x80; break;
  case GDK_greater:
    keyports[9]&=~0x80; break;
  case GDK_slash:
    keyports[6]&=~0x20; break;
  case GDK_question:
    keyports[6]&=~0x20; break;
  case GDK_backslash: case GDK_bar:
    keyports[7]&=~0x04; break;

  default:
    if(event->keyval>=32 && event->keyval<128)
      keyports[keytable[event->keyval-32].port]&=
      		~keytable[event->keyval-32].mask;
  }

return(TRUE);
}


static gint focus_change_event(GtkWidget *widget,GdkEventFocus *event)
{
if(event->in)
  gdk_key_repeat_disable(),need_keyrep_restore=1;
else
  gdk_key_repeat_restore(),need_keyrep_restore=0;

return(FALSE);	/* just in case anything else needs it */
}


/* redraw the screen. Returns non-zero if `contents' of image changed.
 * If they have, and areap is non-NULL, *areap will then contain the area
 * which needs to be redrawn.
 */
int update_scrn(int force,struct update_area *areap)
{
static unsigned char oldscrn[60*128];
static int count=0,first=1;
unsigned char *ramptr,*oldptr;
int x,y,val,mask,a,b;
int changed=0;
int xmin,xmax,ymin,ymax,xsc,ysc;
int lines=nc200?128:64;

/* only do it every 1/Nth
 * (non-zero `force' arg overrides this)
 */
if(first) force=1,first=0;

count++;
if(count<scrn_freq && !force) return(0);

count=0;

xmin=480*scale; xmax=-1;
ymin=lines*scale;  ymax=-1;

ramptr=mem+RAM_START+scrnaddr;
oldptr=oldscrn;

for(y=ysc=0;y<lines;y++,ramptr+=4,ysc+=scale)
  if(memcmp(ramptr,oldptr,60)==0 && !force_full_redraw)
    ramptr+=60,oldptr+=60;
  else
    {
    changed=1;
    if(ysc<ymin) ymin=ysc;
    if(ysc+scale-1>ymax) ymax=ysc+scale-1;
    
    /* redraw the line */
    for(x=xsc=0;x<480;ramptr++,oldptr++)
      if(*ramptr==*oldptr && !force_full_redraw)
        x+=8,xsc+=8*scale;
      else
        {
        if(xsc<xmin) xmin=xsc;
        /* we test for max after (saves some maths :-)) */
        
        for(mask=128,val=*ramptr;mask;mask>>=1,x++,xsc+=scale)
          if(scale==1)
            gdk_image_put_pixel(image,x,y,(val&mask)?blackpix:whitepix);
          else
            {
            /* XXX is this really the fastest way? */
            for(b=0;b<scale;b++)
              for(a=0;a<scale;a++)
                gdk_image_put_pixel(image,xsc+a,ysc+b,
                				(val&mask)?blackpix:whitepix);
            }
        
        /* xsc now points to first pixel not updated */
        if(xsc-1>xmax) xmax=xsc-1;
        }
    
    memcpy(oldptr-60,ramptr-60,60);
    }

force_full_redraw=0;

if(changed && areap)
  {
  areap->xmin=xmin;
  areap->xmax=xmax;
  areap->ymin=ymin;
  areap->ymax=ymax;
  }

return(changed);
}



/* grey out any scale buttons which wouldn't do anything if clicked.
 * (This stuff probably means I don't actually need range checking
 * in cb_scale_{down,up}, but I'm not taking that chance ta. ;-))
 */
void set_scale_buttons_sensitivity()
{
gtk_widget_set_sensitive(sdown_button,(scale>1));
gtk_widget_set_sensitive(sup_button,(scale<4));
}


/* set hsize/vsize, size of scrolly win, and size of drawing area. */
void set_size(void)
{
int screen_width=gdk_screen_width();
int win_width,win_height;

hsize=480*scale; vsize=(nc200?128:64)*scale;
win_width=hsize+BORDER_WIDTH*2*scale;
win_height=vsize+BORDER_WIDTH*2*scale;
gtk_drawing_area_size(GTK_DRAWING_AREA(drawing_area),win_width,win_height);

win_width+=SW_BORDER_WIDTH;
win_height+=SW_BORDER_WIDTH+
	GTK_SCROLLED_WINDOW(scrolled_window)->hscrollbar->allocation.height+3;

if(win_width>screen_width-HORIZ_MARGIN_WIDTH)
  win_width=screen_width-HORIZ_MARGIN_WIDTH;

gtk_widget_set_usize(scrolled_window,win_width,win_height);
}


void scale_change_fixup(void)
{
int win_width,win_height;

/* grey out buttons which wouldn't do anything now */
set_scale_buttons_sensitivity();

set_size();

win_width=drawing_area->allocation.width;
win_height=drawing_area->allocation.height;

/* draw border */
gdk_draw_rectangle(drawing_area->window,drawing_area->style->white_gc,TRUE,
	0,0,win_width,win_height);

/* force the scrolly window to notice :-) */
gtk_widget_queue_resize(drawing_area);

/* lose old image, and allocate new one */
gdk_image_destroy(image);
image=gdk_image_new(GDK_IMAGE_FASTEST,gdk_visual_get_system(),hsize,vsize);

/* draw screen into new image */
force_full_redraw=1;
update_scrn(1,NULL);
}


static gint cb_scale_down(GtkWidget *widget,gpointer junk)
{
if(scale>1)
  {
  scale--;
  scale_change_fixup();
  }

return(TRUE);
}


static gint cb_scale_up(GtkWidget *widget,gpointer junk)
{
if(scale<4)
  {
  scale++;
  scale_change_fixup();
  }

return(TRUE);
}


/* stop dead */
static gint cb_stop(GtkWidget *widget,gpointer junk)
{
dontpanic(0);	/* doesn't return */
return(TRUE);	/* but keep -Wall happy :-) */
}


/* stop with NMI */
static gint cb_stop_nmi(GtkWidget *widget,gpointer junk)
{
do_nmi=1;

return(TRUE);
}




void initwindow(void)
{
/* basic layout is like this:
 *   (vbox in window contains all this)
 *  ________________________________________  (-/+ control pixel scaling)
 * |[-] [+]      [abrupt stop] [Exit (NMI)] | (hbox in vbox contains these)
 * |----------------------------------------| 
 * |                                        |
 * | <NC screen, possibly partial>          | (drawing_area...
 * |________________________________________|
 * |[<===<scrollbar>======================>]|   ...in scrolled_window)
 * `----------------------------------------'
 */
GtkWidget *vbox,*hbox;
GtkWidget *button;
GdkPixmap *icon;
GdkBitmap *mask;
GdkGCValues gcval;
GtkWidget *ebox;


window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
gtk_signal_connect(GTK_OBJECT(window),"destroy",
			(GtkSignalFunc)destroy,NULL);
gtk_window_set_title(GTK_WINDOW(window),
			nc200?"Gnc100em (NC200)":"Gnc100em");
gtk_container_set_border_width(GTK_CONTAINER(window),0);
gtk_window_set_policy(GTK_WINDOW(window),TRUE,TRUE,TRUE);

ebox=gtk_event_box_new();
gtk_container_add(GTK_CONTAINER(window),ebox);
gtk_widget_show(ebox);


vbox=gtk_vbox_new(FALSE,0);
gtk_container_add(GTK_CONTAINER(ebox),vbox);
gtk_widget_show(vbox);

hbox=gtk_hbox_new(FALSE,0);
gtk_container_set_border_width(GTK_CONTAINER(hbox),2);
gtk_box_pack_start(GTK_BOX(vbox),hbox,FALSE,FALSE,0);
gtk_widget_show(hbox);

/* NB: Since I can't seem to figure out an elegant way to disable
 * the default key bindings (in particular, Tab/Shift-Tab which, ah, aren't
 * good for our mental health), I have to explicitly disable them on
 * every widget (other than the boxes). Joy. :-(
 */

/* the scale down/up buttons */
button=gtk_button_new_with_label("Scale down");
gtk_box_pack_start(GTK_BOX(hbox),button,FALSE,FALSE,2);
gtk_signal_connect(GTK_OBJECT(button),"clicked",
		GTK_SIGNAL_FUNC(cb_scale_down),NULL);
gtk_widget_show(button);
GTK_WIDGET_UNSET_FLAGS(button,GTK_CAN_FOCUS);
sdown_button=button;

button=gtk_button_new_with_label("Scale up");
gtk_box_pack_start(GTK_BOX(hbox),button,FALSE,FALSE,2);
gtk_signal_connect(GTK_OBJECT(button),"clicked",
		GTK_SIGNAL_FUNC(cb_scale_up),NULL);
gtk_widget_show(button);
GTK_WIDGET_UNSET_FLAGS(button,GTK_CAN_FOCUS);
sup_button=button;

set_scale_buttons_sensitivity();

/* the F10/F8 buttons :-) */
button=gtk_button_new_with_label("Exit nicely (NMI)");
gtk_box_pack_end(GTK_BOX(hbox),button,FALSE,FALSE,2);
gtk_signal_connect(GTK_OBJECT(button),"clicked",
		GTK_SIGNAL_FUNC(cb_stop_nmi),NULL);
gtk_widget_show(button);
GTK_WIDGET_UNSET_FLAGS(button,GTK_CAN_FOCUS);

button=gtk_button_new_with_label("Abrupt stop");
gtk_box_pack_end(GTK_BOX(hbox),button,FALSE,FALSE,2);
gtk_signal_connect(GTK_OBJECT(button),"clicked",
		GTK_SIGNAL_FUNC(cb_stop),NULL);
gtk_widget_show(button);
GTK_WIDGET_UNSET_FLAGS(button,GTK_CAN_FOCUS);


/* now the scrolled window, and the drawing area which goes into it. */
scrolled_window=gtk_scrolled_window_new(NULL,NULL);
GTK_WIDGET_UNSET_FLAGS(scrolled_window,GTK_CAN_FOCUS);

/* first `POLICY' is horiz, second is vert */
gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				GTK_POLICY_ALWAYS,GTK_POLICY_AUTOMATIC);

gtk_box_pack_start(GTK_BOX(vbox),scrolled_window,TRUE,TRUE,0);
gtk_widget_show(scrolled_window);

drawing_area=gtk_drawing_area_new();
gtk_signal_connect(GTK_OBJECT(drawing_area), "expose_event",
		(GtkSignalFunc) expose_event, NULL);
/* need to ask for expose. */
gtk_widget_set_events(drawing_area,GDK_EXPOSURE_MASK);

gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window),
					drawing_area);

gtk_widget_show(drawing_area);
GTK_WIDGET_UNSET_FLAGS(drawing_area,GTK_CAN_FOCUS);


/* set it up to get keypress/keyrelease */
gtk_signal_connect(GTK_OBJECT(window),"key_press_event",
		(GtkSignalFunc)key_press_event,NULL);
gtk_signal_connect(GTK_OBJECT(window),"key_release_event",
		(GtkSignalFunc)key_release_event,NULL);

/* need to get focus change events so we can fix the auto-repeat
 * when focus is changed, not just when we start/stop.
 */
gtk_signal_connect(GTK_OBJECT(window),"focus_in_event",
		(GtkSignalFunc)focus_change_event,NULL);
gtk_signal_connect(GTK_OBJECT(window),"focus_out_event",
		(GtkSignalFunc)focus_change_event,NULL);

gtk_widget_set_events(window,
	GDK_KEY_PRESS_MASK|GDK_KEY_RELEASE_MASK|GDK_FOCUS_CHANGE_MASK);


/* get things roughly the right size */
set_size();

gtk_widget_show(window);

/* do it again now we know the scrollbar's height.
 * XXX this is really dire, but I can't seem to find a better way that
 * works. Well, doing gtk_widget_realize() before the above set_size()
 * does it ok, but (unsurprisingly?) gives a `critical' warning!
 */
set_size();


/* set icon */
icon=gdk_pixmap_create_from_xpm_d(window->window,&mask,NULL,nc100_xpm);
gdk_window_set_icon(window->window,NULL,icon,mask);


/* allocate initial backing image for drawing area */
image=gdk_image_new(GDK_IMAGE_FASTEST,gdk_visual_get_system(),hsize,vsize);

/* get black/white colour values so we can write the image more quickly. */
gdk_gc_get_values(drawing_area->style->black_gc,&gcval);
blackpix=gcval.foreground.pixel;
gdk_gc_get_values(drawing_area->style->white_gc,&gcval);
whitepix=gcval.foreground.pixel;

update_scrn(1,NULL);	/* the 1 forces it to ignore scrn_freq, and run */

/* that's all folks */
}



void do_interrupt(void)
{
static int scount=0;
static struct update_area area;
int flush=0;

if(update_scrn(0,&area))
  {
  int ox=(drawing_area->allocation.width-(hsize+BORDER_WIDTH*2*scale))/2;
  int oy=(drawing_area->allocation.height-(vsize+BORDER_WIDTH*2*scale))/2;

  /* need to redraw drawing_area from image */
  gdk_draw_image(drawing_area->window,
	drawing_area->style->fg_gc[GTK_WIDGET_STATE(drawing_area)],
	image,
        area.xmin,area.ymin,
        ox+BORDER_WIDTH*scale+area.xmin,oy+BORDER_WIDTH*scale+area.ymin,
        area.xmax-area.xmin+1,area.ymax-area.ymin+1);
  
  /* this makes sure we flush fairly regularly when screen is
   * updating a lot. (I doubt it's really necessary though.)
   */
  flush=1;
  }

/* give GTK+ a chance to do its thang. :-) */
while(!signal_int_flag && gtk_events_pending())
  gtk_main_iteration();

if(flush)
  gdk_flush();

scount++;
if(scount==4) scount=0,serout_flush();
}



int main(int argc,char *argv[])
{
gtk_init(&argc,&argv);

parseoptions(argc,argv);

/* set scale from default in common.c */
scale=default_scale;
hsize=480*scale; vsize=(nc200?128:64)*scale;

common_init();

if(nc200) keytable=nc200_keytable;

/* sort out all the GTK+ stuff (must happen after RAM (screen) is loaded) */
initwindow();

/* give it a chance to draw it, as it otherwise won't for a while */
while(gtk_events_pending())
  gtk_main_iteration();

startsigsandtimer();
mainloop();

/* not reached */
exit(0);
}
