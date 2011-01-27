/* nc100em, an Amstrad NC100 emulator.
 * Copyright (C) 1994 Ian Collier. nc100em changes (C) 1996,1999 Russell Marks.
 *
 * libdir.c - libdir() routine. Separated out from common.c as
 *		both the emulators and zcntools need it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libdir.h"


/* return full path for file in libdir. Also makes libdir if it
 * doesn't yet exist.
 */
char *libdir(char *file)
{
static char buf[1024];
char *nchome=getenv("NC100EM_HOME");

if(nchome)
  {
  mkdir(nchome,0777);
  snprintf(buf,sizeof(buf),"%s/%s",nchome,file);
  }
else
  {
  char *home=getenv("HOME");
  
  if(!home)
    snprintf(buf,sizeof(buf),"%s",file);	/* current dir */
  else
    {
    /* XXX slightly crappy way to do it:-) */
    snprintf(buf,sizeof(buf),"%s/nc100",home);
    mkdir(buf,0777);
    
    snprintf(buf,sizeof(buf),"%s/nc100/%s",home,file);
    }
  }

return(buf);
}
