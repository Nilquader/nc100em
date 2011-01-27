/* dnc100em, a SDL-based NC100 emulator for X.
 * Copyright (C) 1999 Russell Marks.
 * SDL Version (C) 2009 by Nilquader of SPRING
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
#include <math.h> /* for sin() used by audio interface */

#include <SDL/SDL.h>


#include "common.h"
#include "z80.h"

// for debugger
int breakpoint;

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

/* sample frequencdy for audio output */
#define AUDIO_SAMPLE_FREQ 44100
void mixaudio(void *unused, Uint8 *stream, int len);  /* prototype -  create mixed audio data stream */

Uint32 blackpix,whitepix;

char *cmd_name="dnc100em";

int scale;
int hsize,vsize;

Uint32 initflags = SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO;  /* parts of SDL to initialize */
SDL_Surface *screen;
Uint8  video_bpp = 0;
Uint32 videoflags = SDL_SWSURFACE;
SDL_Event event;

int need_keyrep_restore=0;


/* size of minimum rectangle of image which needs to be redrawn. */
struct update_area
  {
  int xmin,xmax;
  int ymin,ymax;
  };

/* state of pointer device */
struct mousedata {
	int mx,my;	// Position
	int button; // Mouse button mask
} mouse_pointer = {0,0,0};		

void closedown(void)
{
/* image may well be using shared memory, so it's probably a Good Idea
 * to blast it :-)
 */
 // missing: free SDL image 
 serial_uninit();
}

void dontpanic(int a) {
	closedown();
	writecard(cardmem);
	SDL_Quit();
	exit(0);
}

void dontpanic_nmi(void) {
	closedown();
	writeram(mem+RAM_START);
	writecard(cardmem);
	SDL_Quit();
	exit(0);
}

/*
 * Set the pixel at (x, y) to the given value
 * NOTE: The surface must be locked before calling this!
 */
void putpixel(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to set */
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        *p = pixel;
        break;

    case 2:
        *(Uint16 *)p = pixel;
        break;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = (pixel >> 16) & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = pixel & 0xff;
        } else {
            p[0] = pixel & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = (pixel >> 16) & 0xff;
        }
        break;

    case 4:
        *(Uint32 *)p = pixel;
        break;
    }
}

/* get mouse state. x/y should not be overwritten if pointer is not
 * in window. buttons returned as *mbutp are l=1,m=2,r=4.
 */
void get_mouse_state(int *mxp,int *myp,int *mbutp) {
	*mxp=mouse_pointer.mx/scale;
	*myp=mouse_pointer.my/scale;
	*mbutp=mouse_pointer.button;
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
            // gdk_image_put_pixel(image,x,y,(val&mask)?blackpix:whitepix);
            putpixel(screen,x,y,(val&mask)?blackpix:whitepix);
          else
            {
            /* XXX is this really the fastest way? */
            for(b=0;b<scale;b++)
              for(a=0;a<scale;a++)
                putpixel(screen,xsc+a,ysc+b,(val&mask)?blackpix:whitepix);
            }
        
        /* xsc now points to first pixel not updated */
        if(xsc-1>xmax) xmax=xsc-1;
        }
    
    memcpy(oldptr-60,ramptr-60,60);
    }

force_full_redraw=0;
SDL_Flip(screen);

if(changed && areap)
  {
  areap->xmin=xmin;
  areap->xmax=xmax;
  areap->ymin=ymin;
  areap->ymax=ymax;
  }

return(changed);
}

/* set hsize/vsize, size of scrolly win, and size of drawing area. */
void set_size(void)
{
  // runtime size change missing
}


void scale_change_fixup(void)
{
/* missing */
	
/* draw screen into new image */
force_full_redraw=1;
update_scrn(1,NULL);
}

void initwindow(void) { /* SDL library and Video initilaization */	
	/* Initialize the SDL library */
	if ( SDL_Init(initflags) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}
	
	/* Set appropriate video mode */
	screen=SDL_SetVideoMode(hsize, vsize, video_bpp, videoflags);
	if (screen == NULL) {
		fprintf(stderr, "Couldn't set %ix%ix%d video mode: %s\n",
		hsize, vsize, video_bpp, SDL_GetError());
		SDL_Quit();
		exit(2);
	}
	
	/* Set coorect size */
	set_size();
	
	/* set window title */
	SDL_WM_SetCaption(nc200?"nc100em (NC200)":nc150?"nc100em (NC150)":"nc100em",0);
	update_scrn(1,NULL);	/* the 1 forces it to ignore scrn_freq, and run */
	
	/* define colors for black and white pixels */
	blackpix = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
	whitepix = SDL_MapRGB(screen->format, 0xff, 0xff, 0xff);
	
	/* done */
}

void initaudio(void) { /* SDL audio initialization */
	SDL_AudioSpec *desired, *obtained; /* audio specs */

	/* Allocate a desired SDL_AudioSpec */
	desired = (SDL_AudioSpec *) malloc(sizeof(SDL_AudioSpec));
	
	/* Allocate space for the obtained SDL_AudioSpec */
	obtained = (SDL_AudioSpec *) malloc(sizeof(SDL_AudioSpec));
	
	/* choose a samplerate and audio-format */
	desired->freq = AUDIO_SAMPLE_FREQ;
	desired->format = AUDIO_S8;
	desired->channels = 1;
	desired->samples = 1024; /* sample buffer */
	
	/* audio synthesis callback function */
	desired->callback=mixaudio;
	desired->userdata=NULL;
	
	/* Open the audio device and start playing sound! */
	if ( SDL_OpenAudio(desired, obtained) < 0 ) {
		fprintf(stderr, "AudioMixer, Unable to open audio: %s\n", SDL_GetError());
		exit(3);
	}
	SDL_PauseAudio(0);

	/* done */
}

void mixaudio(void *unused, Uint8 *stream, int len) { /* create mixed audio data stream */
	int i;
	int channel1 = 0, channel2 = 0, outputValue;
	static int fase1 = 0;
	static int fase2 = 0;
	
    unsigned int bytesPerPeriod1 = 0;
    unsigned int bytesPerPeriod2 = 0;
	
	if (freq1 > (AUDIO_SAMPLE_FREQ / 2)) freq1 = 0;
	if (freq2 > (AUDIO_SAMPLE_FREQ / 2)) freq2 = 0; // audio will be diabled, if frequency is too high.
	
	if (freq1 > 0) bytesPerPeriod1 = AUDIO_SAMPLE_FREQ / freq1;
	if (freq2 > 0) bytesPerPeriod2 = AUDIO_SAMPLE_FREQ / freq2;

    for (i=0;i<len;i++) {
        if (freq1 > 0) channel1 = (int)(50*sin(fase1*6.28/bytesPerPeriod1));
        if (freq2 > 0) channel2 = (int)(50*sin(fase2*6.28/bytesPerPeriod2));

        outputValue = channel1 + channel2;           // just add the channels
        if (outputValue > 128) outputValue = 128;        // and clip the result
        if (outputValue < -127) outputValue = -127;      // this seems a crude method, but works very well
        
        stream[i] = outputValue;

		if (freq1 > 0) {
        	fase1++;
        	fase1 %= bytesPerPeriod1;
		}
		if (freq2 > 0) {
        	fase2++;
        	fase2 %= bytesPerPeriod2;
		}
    }
}

/* handle keypress events */
void event_keydown(void) {
	//printf("Keypressed: %i, modifier: %i\n", event.key.keysym.sym, event.key.keysym.mod);
	switch(event.key.keysym.sym) {
		case SDLK_F5: case SDLK_F10:
			do_nmi=1;
			break;
		case SDLK_F8:
			dontpanic(0);
			/* doens't return */
			break;
		case SDLK_F7: // launch debugger
			breakpoint = 1;
			break;
	
		case SDLK_INSERT: /* FUNCTION */
		case SDLK_LSUPER: case SDLK_RSUPER:
		case SDLK_LMETA: case SDLK_RMETA:
			keyports[1]|=0x01; break;
		case SDLK_HOME: /* also works as SECRET/MENU key */
			keyports[7]|=0x10; break;
		case SDLK_RETURN:
			keyports[0]|=0x10; break;
		case SDLK_LCTRL: case SDLK_RCTRL:
			keyports[1]|=0x02; break;
		case SDLK_LSHIFT:
			keyports[0]|=0x01; break;
		case SDLK_RSHIFT:
			keyports[0]|=0x02; break;
		case SDLK_LALT: case SDLK_RALT:
			keyports[2]|=0x02; break;
		case SDLK_CAPSLOCK:
			keyports[2]|=0x01; break;
		case SDLK_DELETE:
			keyports[6]|=0x04; break;
		case SDLK_BACKSPACE:
			keyports[9]|=0x04; break;
		case SDLK_ESCAPE:
			keyports[1]|=0x04; break;
		case SDLK_TAB:
			keyports[2]|=0x08; break;
		case SDLK_UP:
			keyports[7]|=0x08; break;
		case SDLK_DOWN:
			keyports[6]|=0x02; break;
		case SDLK_LEFT:
			keyports[0]|=0x08; break;
		case SDLK_RIGHT:
			keyports[6]|=0x08; break;
		case SDLK_MINUS:
			keyports[8]|=0x02; break;
		case SDLK_UNDERSCORE:
			keyports[8]|=0x02; break;
		case SDLK_EQUALS:
			if(nc200) keyports[7]|=0x02; else keyports[7]|=0x01; break;
		case SDLK_PLUS:
			if(nc200) keyports[7]|=0x02; else keyports[7]|=0x01; break;
		case SDLK_SEMICOLON:
			keyports[9]|=0x10; break;
		case SDLK_COLON:
			keyports[9]|=0x10; break;
		case SDLK_BACKQUOTE:
			keyports[8]|=0x10; break;
		case SDLK_QUOTEDBL:
			keyports[3]|=0x02; break;
		case SDLK_EXCLAIM:
			keyports[2]|=0x04; break;
		case SDLK_AT:
			keyports[8]|=0x10; break;
		case SDLK_HASH:
			keyports[6]|=0x10; break;
		case SDLK_DOLLAR:
			if(nc200) keyports[0]|=0x04; else keyports[4]|=0x01; break;
		case SDLK_CARET:
			if(nc200) keyports[2]|=0x40; else keyports[6]|=0x01; break;
		case SDLK_ASTERISK:
			if(nc200) keyports[4]|=0x01; else keyports[8]|=0x01; break;
		case SDLK_LEFTPAREN:
			if(nc200) keyports[1]|=0x80; else keyports[9]|=0x02; break;
		case SDLK_RIGHTPAREN:
			if(nc200) keyports[9]|=0x02; else keyports[9]|=0x01; break;
		case SDLK_COMMA:
			keyports[8]|=0x80; break;
		case SDLK_LESS:
			keyports[8]|=0x80; break;
		case SDLK_PERIOD:
			keyports[9]|=0x80; break;
		case SDLK_GREATER:
			keyports[9]|=0x80; break;
		case SDLK_SLASH:
			keyports[6]|=0x20; break;
		case SDLK_QUESTION:
			keyports[6]|=0x20; break;
		case SDLK_BACKSLASH:
			keyports[7]|=0x04; break;
		// german umlauts mapped to corresponding keys on US layout
		case SDLK_WORLD_2: // Umlaut-U
			keyports[8]|=0x08; break;
		case SDLK_WORLD_3: // Umlaut-A
			keyports[8]|=0x10; break;
		case SDLK_WORLD_4: // Umlaut-O
			keyports[9]|=0x10; break;
		default:
		    if(event.key.keysym.sym>=32 && event.key.keysym.sym<128)
		    	keyports[keytable[event.key.keysym.sym-32].port]|=
		    	keytable[event.key.keysym.sym-32].mask;
	}
}

/* handle key release events */
void event_keyup(void) {
	// printf("Key released: %i, modifier: %i\n", event.key.keysym.sym, event.key.keysym.mod);
	switch(event.key.keysym.sym) {
		case SDLK_INSERT:
		case SDLK_LSUPER: case SDLK_RSUPER:
		case SDLK_LMETA: case SDLK_RMETA:
			keyports[1]&=~0x01; break;
		case SDLK_HOME: /* also works as SECRET/MENU key */
			keyports[7]&=~0x10; break;
		case SDLK_RETURN:
			keyports[0]&=~0x10; break;
		case SDLK_LCTRL: case SDLK_RCTRL:
			keyports[1]&=~0x02; break;
		case SDLK_LSHIFT:
			keyports[0]&=~0x01; break;
		case SDLK_RSHIFT:
			keyports[0]&=~0x02; break;
		case SDLK_LALT: case SDLK_RALT:
			keyports[2]&=~0x02; break;
		case SDLK_CAPSLOCK:
			keyports[2]&=~0x01; break;
		case SDLK_DELETE:
			keyports[6]&=~0x04; break;
		case SDLK_BACKSPACE:
			keyports[9]&=~0x04; break;
		case SDLK_ESCAPE:
			keyports[1]&=~0x04; break;
		case SDLK_TAB:
			keyports[2]&=~0x08; break;
		case SDLK_UP:
			keyports[7]&=~0x08; break;
		case SDLK_DOWN:
			keyports[6]&=~0x02; break;
		case SDLK_LEFT:
			keyports[0]&=~0x08; break;
		case SDLK_RIGHT:
			keyports[6]&=~0x08; break;
		case SDLK_MINUS:
			keyports[8]&=~0x02; break;
		case SDLK_UNDERSCORE:
			keyports[8]&=~0x02; break;
		case SDLK_EQUALS:
			if(nc200) keyports[7]&=~0x02; else keyports[7]&=~0x01; break;
		case SDLK_PLUS:
			if(nc200) keyports[7]&=~0x02; else keyports[7]&=~0x01; break;
		case SDLK_SEMICOLON:
			keyports[9]&=~0x10; break;
		case SDLK_COLON:
			keyports[9]&=~0x10; break;
		case SDLK_BACKQUOTE:
			keyports[8]&=~0x10; break;
		case SDLK_QUOTEDBL:
			keyports[3]&=~0x02; break;
		case SDLK_EXCLAIM:
			keyports[2]&=~0x04; break;
		case SDLK_AT:
			keyports[8]&=~0x10; break;
		case SDLK_HASH:
			keyports[6]&=~0x10; break;
		case SDLK_DOLLAR:
			if(nc200) keyports[0]&=~0x04; else keyports[4]&=~0x01; break;
		case SDLK_CARET:
			if(nc200) keyports[2]&=~0x40; else keyports[6]&=~0x01; break;
		case SDLK_ASTERISK:
			if(nc200) keyports[4]&=~0x01; else keyports[8]&=~0x01; break;
		case SDLK_LEFTPAREN:
			if(nc200) keyports[1]&=~0x80; else keyports[9]&=~0x02; break;
		case SDLK_RIGHTPAREN:
			if(nc200) keyports[9]&=~0x02; else keyports[9]&=~0x01; break;
		case SDLK_COMMA:
			keyports[8]&=~0x80; break;
		case SDLK_LESS:
			keyports[8]&=~0x80; break;
		case SDLK_PERIOD:
			keyports[9]&=~0x80; break;
		case SDLK_GREATER:
			keyports[9]&=~0x80; break;
		case SDLK_SLASH:
			keyports[6]&=~0x20; break;
		case SDLK_QUESTION:
			keyports[6]&=~0x20; break;
		case SDLK_BACKSLASH:
			keyports[7]&=~0x04; break;
		// german umlauts mapped to corresponding keys on US layout
		case SDLK_WORLD_2: // Umlaut-U
			keyports[8]&=~0x08; break;
		case SDLK_WORLD_3: // Umlaut-A
			keyports[8]&=~0x10; break;
		case SDLK_WORLD_4: // Umlaut-O
			keyports[9]&=~0x10; break;			
		default:
		    if(event.key.keysym.sym>=32 && event.key.keysym.sym<128)
		    	keyports[keytable[event.key.keysym.sym-32].port]&=
		    	~keytable[event.key.keysym.sym-32].mask;
	}
}


/* checks for SDL events */
void check_events(void) {
	while (SDL_PollEvent(&event)) {   //Poll our SDL key event for anything that could happen
		switch(event.type) {
			case SDL_KEYDOWN: // handle keystrokes
				event_keydown();
				break;
			case SDL_KEYUP: // handle key releases
				event_keyup();
				break;
			case SDL_MOUSEMOTION:
				mouse_pointer.mx = event.motion.x;
				mouse_pointer.my = event.motion.y;
				break;
			case SDL_MOUSEBUTTONDOWN: // handle mouse button presses
				switch (event.button.button) {
					case SDL_BUTTON_LEFT:
						mouse_pointer.button|=0x01;
						break;
					case SDL_BUTTON_MIDDLE:
						mouse_pointer.button|=0x02;
						break;
					case SDL_BUTTON_RIGHT:
						mouse_pointer.button|=0x04;
						break;
				}
				break;
			case SDL_MOUSEBUTTONUP: // handle mouse button release
				switch (event.button.button) {
					case SDL_BUTTON_LEFT:
						mouse_pointer.button&=~0x01;
						break;
					case SDL_BUTTON_MIDDLE:
						mouse_pointer.button&=~0x02;
						break;
					case SDL_BUTTON_RIGHT:
						mouse_pointer.button&=~0x04;
						break;
				}
				break;
			case SDL_QUIT: // quit the program
				do_nmi=1;
				break;
		}
	}
}


void do_interrupt(void)
{
static int count=0;
static int scount=0;

/* only do refresh() every 1/Nth */
count++;
if(count>=scrn_freq) {	
  count=0;
  // refresh();
  update_scrn(1,NULL);
}
check_events();

scount++;
if(scount==4) scount=0,serout_flush();
}


int main(int argc,char *argv[])
{
/* parse cmdline options */
parseoptions(argc,argv);

/* set scale from default in common.c */
scale=default_scale;
hsize=480*scale; vsize=(nc200?128:64)*scale;

common_init();

if(nc200) keytable=nc200_keytable;

/* sort out all the SDL stuff  */
initwindow();
initaudio();

/* give it a chance to draw it, as it otherwise won't for a while */
/* while(gtk_events_pending())
  gtk_main_iteration(); */

startsigsandtimer();
mainloop();

/* not reached */
exit(0);
}
