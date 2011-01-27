/* zcntools - an mtools-like utility for working on ZCN disk images.
 * Copyright (C) 1999,2000 Russell Marks.
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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/mman.h>

#include "libdir.h"
#include "version.h"

char card_file[1024];
unsigned char *cardmem;
int card_size=0;
int default_drive=0,default_user=0;
int drive_valid[4]={1,0,0,0};
int drive_formatted[4]={0,0,0,0};
int card_changed=0;

char *cmd_name;


int getfilesize(char *fname)
{
FILE *in;
int filesz;

if((in=fopen(fname,"rb"))==NULL)
  return(-1);
fseek(in,0,SEEK_END);
filesz=ftell(in);
fclose(in);
return(filesz);
}


int is_zcn_fmt(int drv)
{
return(strncmp(cardmem+drv*256*1024+2,"ZCN1",4)==0);
}



/* this also does init stuff like checking drives */
void readcard_and_init(void)
{
FILE *in;
int f;

card_size=((getfilesize(card_file)/1024)&~15);
if(card_size>1024) card_size=1024;

/* we open read/write in case we're using mmap() */
if((in=fopen(card_file,"r+"))!=NULL)
  {
#ifndef USE_MMAP_FOR_CARD
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
  fprintf(stderr,"%s: couldn't load mem card file `%s'.\n",cmd_name,card_file);
  exit(1);
  }

if(card_size<32)
  fprintf(stderr,"%s: card must be at least 32k!\n",cmd_name),exit(1);

/* work out which drives of a: b: c: d: are valid */
drive_valid[0]=1;
if(card_size>=512) drive_valid[1]=1;
if(card_size>=768) drive_valid[2]=1;
if(card_size>=1024) drive_valid[3]=1;

/* see which are formatted */
for(f=0;f<4;f++)
  if(drive_valid[f] && is_zcn_fmt(f))
    drive_formatted[f]=1;
}


void writecard(void)
{
#ifdef USE_MMAP_FOR_CARD
if(munmap(cardmem,card_size*1024)==-1)
  fprintf(stderr,"%s: couldn't munmap() mem card file `%s'.\n",
  	cmd_name,card_file);
#else
FILE *out;

/* don't bother if we don't have to. */
if(!card_changed) return;

if((out=fopen(card_file,"wb"))==NULL)
  fprintf(stderr,"%s: couldn't write mem card to `%s'.\n",cmd_name,card_file);
else
  {
  if(fwrite(cardmem,1024,card_size,out)!=card_size)
    {
    fprintf(stderr,"%s: error writing mem card to `%s'.\n",cmd_name,card_file);
    fclose(out);
    exit(1);
    }
  fclose(out);
  }
#endif /* !USE_MMAP_FOR_CARD */
}


/* a rather less friendly variant of matchfcb_wild(),
 * used by the low-level disk I/O stuff (file reading etc.).
 */
unsigned char *matchfcb_exact(int drive,int user,char *name83,
		int anyeras,int extent)
/*
char *name83;	 FILENAMEEXT :-) - 11-char uppercase
int anyeras;	 match any available entry
int extent;	 extent to match, -1 matches any
*/
{
unsigned char *drvptr;	/* ptr to drive */
unsigned char *dirptr;	/* ptr to first entry */
int nument;	/* num. dir. entries */
int f;

if(!drive_valid[drive] || !drive_formatted[drive]) return(NULL);

drvptr=cardmem+256*1024*drive;
dirptr=drvptr+1024*(1+drvptr[8]);
nument=drvptr[9]*32;

for(f=0;f<nument;f++,dirptr+=32)
  {
  if(anyeras && dirptr[0]==0xe5)
    return(dirptr);
  if(!anyeras && dirptr[0]==user && strncmp(dirptr+1,name83,11)==0 &&
      (dirptr[12]==extent || extent==-1))
    return(dirptr);
  }

/* if we got here, couldn't find it */
return(NULL);
}


/* returns free block on current drive, or -1 if none */
int findfreeblock(int drive,int *freecountptr)
{
static char avail[256];
unsigned char *drvptr;	/* ptr to drive */
unsigned char *dirptr;	/* ptr to first entry */
int nument;	/* num. dir. entries */
int f,g,total=0;
int ret=-1;
int numdatablks;
int drvsiz;

if(!drive_valid[drive] || !drive_formatted[drive]) return(-1);

memset(avail,1,256);

drvptr=cardmem+256*1024*drive;
dirptr=drvptr+1024*(1+drvptr[8]);
nument=drvptr[9]*32;

drvsiz=drvptr[6]+256*drvptr[7];
if(drvsiz>256) drvsiz=256;
numdatablks=drvsiz-1-drvptr[8];
for(f=0;f<drvptr[9];f++) avail[f]=0;	/* discount dir. blks */

for(f=0;f<nument;f++,dirptr+=32)
  {
  if(dirptr[0]!=0xe5)
    {
    for(g=16;g<32;g++)
      if(dirptr[g]!=0)
        avail[dirptr[g]]=0;
    }
  }

for(f=0;f<numdatablks;f++)
  if(avail[f])
    {
    total++;
    if(ret==-1) ret=f;
    }

if(freecountptr!=NULL) *freecountptr=total;

return(ret);
}


/* makes a CP/M 8.3 filename. */
char *make83(char *filename)
{
static char cpmname[1024];
int f,namelen;
char *ptr,*ptr2;

memset(cpmname,0,sizeof(cpmname));

if((ptr=strrchr(filename,'/'))==NULL)
  ptr=filename;
else
  ptr++;

/* ok, we need to allow for files like '.wibble' i.e. no chars before the
 * dot. For now, we just add an 'x' as the filename and contract the rest
 * to 3 chars. It's crap, but what do you expect?
 */
if((*ptr)=='.')
  {
  *cpmname='x';
  strcpy(cpmname+1,ptr);
  }
else
  strcpy(cpmname,ptr);

/* find the dot, if there is one */
if((ptr=strchr(cpmname,'.'))==NULL)
  namelen=strlen(cpmname);
else
  namelen=(ptr-cpmname);
  
/* squash the stuff before the dot to 8 chars. */
if(namelen>8) namelen=8;
cpmname[namelen]=0;
  
/* if there was a dot, try to find another dot or zero to end string */
if(ptr!=NULL)
  {
  ptr++;
  if((ptr2=strchr(ptr,'.'))!=NULL)
    *ptr2=0;
  
  if(strlen(ptr)>3) ptr[3]=0;
  memmove(cpmname+8,ptr,strlen(ptr)+1);
  }

for(f=namelen;f<=7;f++) cpmname[f]=32;
for(f=strlen(cpmname+8);f<=2;f++) cpmname[8+f]=32;
cpmname[11]=0;
for(f=0;f<strlen(cpmname);f++)
  cpmname[f]=toupper(cpmname[f]);

return(cpmname);
}


int bfdel(int drive,int user,char *n83)
{
unsigned char *dirptr;

if((dirptr=matchfcb_exact(drive,user,n83,0,-1))==NULL)
  return(0);

card_changed=1;

do
  dirptr[0]=0xe5;
while((dirptr=matchfcb_exact(drive,user,n83,0,-1))!=NULL);

return(1);
}


int bfmake(int drive,int user,char *n83)
{
char *dirptr;
int f;

if(matchfcb_exact(drive,user,n83,0,0))	/* if it exists */
  bfdel(drive,user,n83);

/* find blank dir entry (user ignored here) */
if((dirptr=matchfcb_exact(drive,0,n83,1,0))==NULL)
  {
  fprintf(stderr,"%s: can't make ZCN file, directory full.\n",cmd_name);
  return(0);
  }

/* create the entry */
card_changed=1;
dirptr[0]=user;
strncpy(dirptr+1,n83,11);
for(f=12;f<32;f++) dirptr[f]=0;
return(1);
}


/* read a 128-byte record from n83 at pos */
int bfread(int drive,int user,char *n83,int pos,unsigned char *dma)
{
unsigned char *dirptr,*drvptr;
int extent,ofs;		/* extent and block offset into it */
int blk;

pos&=(~127);	/* make sure it's a multiple of 128 */
extent=pos/16384;
ofs=(pos/1024)%16;

/* find correct dir entry */
if((dirptr=matchfcb_exact(drive,user,n83,0,extent))==NULL)
  return(0);	/* couldn't find extent */

/* get data block number which contains record */
blk=dirptr[16+ofs];

/* if it's zero, almost certainly a read past eof */
if(blk==0) return(0);

/* convert data block num. to real 1k offset from start of drive */
drvptr=((dirptr-cardmem)&(~(256*1024-1)))+cardmem;
blk+=1+drvptr[8];

/* check this block was actually written */
if(ofs*8+(pos/128)%8>=dirptr[15]) return(0);

/* actually read it! */
memcpy(dma,drvptr+blk*1024+pos%1024,128);

return(1);
}


/* write a 128-byte record to n83 at pos */
int bfwrite(int drive,int user,char *n83,int pos,unsigned char *dma)
{
unsigned char *dirptr,*drvptr;
int extent,ofs;		/* extent and block offset into it */
int blk;
int f;

card_changed=1;

pos&=(~127);	/* make sure it's a multiple of 128 */
extent=pos/16384;
ofs=(pos/1024)%16;

/* find correct dir entry */
if((dirptr=matchfcb_exact(drive,user,n83,0,extent))==NULL)
  {
  /* if we couldn't find extent, make one (user ignored here) */
  if((dirptr=matchfcb_exact(drive,0,n83,1,0))==NULL)
    return(0);	/* give up if we couldn't do that */
  
  /* construct it, rather like bfmake */
  dirptr[0]=user;
  strncpy(dirptr+1,n83,11);
  for(f=12;f<32;f++) dirptr[f]=0;
  dirptr[12]=extent;
  }

/* get data block number which contains record */
blk=dirptr[16+ofs];

/* if it's zero, none allocated yet, so do that */
if(blk==0)
  {
  if((blk=findfreeblock(drive,NULL))==-1)
    return(0);	/* disk full */
  
  dirptr[16+ofs]=blk;
  }

/* convert data block num. to real 1k offset from start of drive */
drvptr=((dirptr-cardmem)&(~(256*1024-1)))+cardmem;
blk+=1+drvptr[8];

/* update extent size field */
if(ofs*8+(pos/128)%8+1>dirptr[15])
  dirptr[15]=ofs*8+(pos/128)%8+1;

/* actually write it! */
memcpy(drvptr+blk*1024+pos%1024,dma,128);

return(1);
}


int bfopen(int drive,int user,char *n83)
{
/* just see if the 0th extent is there */
if(matchfcb_exact(drive,user,n83,0,0)) return(1);
return(0);
}


void zcntools_main(void)
{
printf("zcntools " NC100EM_VER
	" - (c) 1999,2000 Russell Marks for improbabledesigns.\n\n");

printf("\n"
"Run as one of the following, making sure you put any ZCN wildcards in quotes:\n"
"\n"
"zcncat du:file		(dump a file from the card to stdout)\n"
"zcndf			(show disk space free on card's drives)\n"
"zcnformat d:		(format a logical drive on the card)\n"
"zcnget du:filespec dir	(copy file(s) from card to specified dir)\n"
"zcninfo			(gives overall info on card)\n"
"zcnls [-l] [du:]	(list files just like ZCN's ls does)\n"
"zcnput file(s) du:	(copy file(s) to card)\n"
"zcnren du:file newname	(rename a file)\n"
"zcnrm du:filespec	(delete file(s))\n"
"zcntools		(gives this help)\n"
"zcnzero			(zero out unused space, for better gzipping etc.)\n"
"\n"
"Default drive/user is drive A: user 0.\n"
"\n"
"Above, `d:' = drive specification, e.g. `d:' :-), and `du:' =\n"
"  drive/user specification, e.g. `a:' (drive A:), `2:' (user 2), `a2:'\n"
"  (drive A: user 2). `filespec' = a filename or wildcard.\n"
"\n");
}


void zcninfo_main(int argc,char *argv[])
{
if(argc!=1)
  {
  zcntools_main();
  return;
  }

printf("Card file is `%s' (%dk).\n",card_file,card_size);
printf("Logical drives:\t\ta: ");
if(drive_valid[1]) printf("b: ");
if(drive_valid[2]) printf("c: ");
if(drive_valid[3]) printf("d: ");
printf("\nFormatted drives:\t");

if(drive_formatted[0] || drive_formatted[1] || drive_formatted[2] || 
    drive_formatted[3])
  {
  if(drive_formatted[0]) printf("a: ");
  if(drive_formatted[1]) printf("b: ");
  if(drive_formatted[2]) printf("c: ");
  if(drive_formatted[3]) printf("d: ");
  }
else
  printf("<none>");

printf("\n");
}


void showname(char *name83)
{
int f,count=0;

for(f=0;f<11;f++)
  {
  if(f==8)
    putchar((memcmp(name83+8,"   ",3)==0)?' ':'.');
  
  if(name83[f]==' ')
    count++;
  else
    putchar(tolower(name83[f]));
  }

for(f=0;f<count;f++)
  putchar(' ');
}


/* grok any d:, u:, or du: at start of filename.
 * sets drive and/or user (or -1 if not specified) via drvp/userp,
 * and returns a pointer to just past any drive/user spec in the filename.
 * exits if an out-of-range drive or user is given.
 */
char *grok_du(char *fname,int *drvp,int *userp)
{
int drive=0,user=0,drive_set=0,user_set=0;
char *ret=fname;

*drvp=-1; *userp=-1;

/* d: */
if(isalpha(fname[0]) && fname[1]==':')
  {
  drive=tolower(fname[0])-'a';
  drive_set=1;
  ret=fname+2;
  }
else
  /* u: */
  if(isdigit(fname[0]) &&
     (fname[1]==':' ||
      (isdigit(fname[1]) && fname[2]==':')))
    {
    user=atoi(fname);
    user_set=1;
    ret=strchr(fname,':')+1;
    }
  else
    /* du: */
    if(isalpha(fname[0]) &&
       (fname[1]==':' ||
        (isdigit(fname[1]) && fname[2]==':') ||
        (isdigit(fname[1]) && isdigit(fname[2]) && fname[3]==':')))
      {
      drive=tolower(fname[0])-'a';
      user=atoi(fname+1);
      drive_set=user_set=1;
      ret=strchr(fname,':')+1;
      }

if(drive_set)
  {
  if(drive<0 || drive>3)
    fprintf(stderr,"%s: drive letter not in range A..D.\n",cmd_name),exit(1);
  else
    *drvp=drive;
  }

if(user_set)
  {
  if(user<0 || user>15)
    fprintf(stderr,"%s: user number not in range 0..15.\n",cmd_name),exit(1);
  else
    *userp=user;
  }

return(ret);
}


/* returns file size in K
 * simply returns 0 if not found, so check it's there yourself. ;-)
 */
int get_zcn_filesize(int drive,int user,char *name83)
{
unsigned char *drvptr;	/* ptr to drive */
unsigned char *dirptr;	/* ptr to first entry */
int nument;	/* num. dir. entries */
int f;
int rcd_count;

drvptr=cardmem+256*1024*drive;
dirptr=drvptr+1024*(1+drvptr[8]);
nument=drvptr[9]*32;

rcd_count=0;
for(f=0;f<nument;f++,dirptr+=32)
  {
  if(dirptr[0]==user && memcmp(name83,dirptr+1,11)==0)
    rcd_count+=dirptr[15];
  }

return((rcd_count+7)/8);
}


void zcnls_main(int argc,char *argv[])
{
static char names[32*4][12];	/* max of 4 dir blocks */
unsigned char *drvptr;	/* ptr to drive */
unsigned char *dirptr;	/* ptr to first entry */
int nument;	/* num. dir. entries */
int numfiles;	/* num. of actual files */
int f,y,x,ofs;
int width=5;	/* number of files listed across */
int lines;
int show_size=0;
char *ptr;
int drive=-1,user=-1;

if(argc>=2 && strcmp(argv[1],"-l")==0)
  {
  show_size=1;
  width--;
  argc--,argv++;
  }

if(argc>2)
  {
  zcntools_main();
  return;
  }

if(argc==1)
  drive=default_drive,user=default_user;
else
  {
  ptr=grok_du(argv[1],&drive,&user);
  if(*ptr)
    fprintf(stderr,"%s: invalid drive/user specification.\n",cmd_name),exit(1);
  if(drive==-1) drive=default_drive;
  if(user==-1) user=default_user;
  }

if(!drive_valid[drive] || !drive_formatted[drive])
  {
  fprintf(stderr,"%s: invalid or unformatted drive.\n",cmd_name);
  return;
  }

drvptr=cardmem+256*1024*drive;
dirptr=drvptr+1024*(1+drvptr[8]);
nument=drvptr[9]*32;

if(drvptr[9]>4)
  {
  fprintf(stderr,"%s: too many directory blocks.\n",cmd_name);
  return;
  }

numfiles=0;
for(f=0;f<nument;f++,dirptr+=32)
  {
  if(dirptr[0]==user && dirptr[12]==0)
    {
    strncpy(names[numfiles],dirptr+1,11);
    names[numfiles][11]=0;
    numfiles++;
    }
  }

if(numfiles==0)
  {
  printf("No files\n");
  return;
  }

qsort(names,numfiles,12,(int (*)(const void *,const void *))strcmp);

/* if there are N files, there are L=(N+width-1)/width lines of
 * output. We have to display horizontally, so the list ptr
 * must start at 0 and skip over L entries each time before
 * display. For the next line, we start at 1, do the same, etc.
 * (lines=L, numfiles=N)
 */
lines=(numfiles+width-1)/width;
for(y=0;y<lines;y++)
  {
  ofs=y;
  for(x=0;x<width;x++,ofs+=lines)
    {
    if(ofs>=numfiles) break;
    /* XXX need to support show_size */
    showname(names[ofs]);
    putchar(' ');
    if(show_size)
      printf("%3dk",get_zcn_filesize(drive,user,names[ofs]));
    if(x<width-1)
      printf("  ");
    }
  putchar('\n');
  }
}


/* returns non-zero on success */
int putfile(char *fn,int dest_drive,int dest_user)
{
static unsigned char dma[128];
FILE *in;
char n83[12];
int f,siz,rcdsiz;

strcpy(n83,make83(fn));

if(!bfmake(dest_drive,dest_user,n83))
  return(0);

if((siz=getfilesize(fn))==-1 || (in=fopen(fn,"rb"))==NULL)
  return(0);

rcdsiz=(siz+127)/128;

for(f=0;f<rcdsiz;f++)
  {
  memset(dma,26,128);	/* set to all ^Z's */
  fread(dma,1,128,in);
  if(!bfwrite(dest_drive,dest_user,n83,f*128,dma))
    {
    fclose(in);
    return(0);
    }
  }

fclose(in);

return(1);
}


/* returns non-zero on success.
 * a NULL dest means write on stdout.
 */
int getfile(int drive,int user,char *n83,char *dest)
{
static unsigned char dma[128];
FILE *out;
int pos=0;

if(!bfopen(drive,user,n83))
  return(0);

if(dest && (out=fopen(dest,"wb"))==NULL)
  return(0);

if(!dest) out=stdout;

while(bfread(drive,user,n83,pos,dma))
  {
  if(fwrite(dma,1,128,out)!=128)
    {
    fclose(out);
    return(0);
    }
  pos+=128;
  }

if(out!=stdout) fclose(out);

return(1);
}


void formatdrive(int drv)
{
unsigned char *drvptr;
int f;

if(!drive_valid[drv])
  {
  fprintf(stderr,"%s: invalid drive.\n",cmd_name);
  return;
  }

if(is_zcn_fmt(drv))
  {
  static char buf[256];
  fprintf(stderr,"%s: drive already formatted - reformat? (y/n) ",cmd_name);
  fgets(buf,sizeof(buf),stdin);
  if(tolower(buf[0])!='y')
    {
    fprintf(stderr,"%s: format aborted!\n",cmd_name);
    return;
    }
  }

/* ok, format it */
card_changed=1;
drvptr=cardmem+256*1024*drv;

drvptr[0]=0xc9; drvptr[1]=0x7e;
strncpy(drvptr+2,"ZCN1",4);
drvptr[6]=card_size%256; drvptr[7]=card_size/256;
drvptr[8]=0; drvptr[9]=2;

for(f=10;f<1024;f++) drvptr[f]=0;

/* write the blank dir blocks */
for(f=1024;f<1024+2048;f++) drvptr[f]=0xe5;

drive_formatted[drv]=1;
}


/* zero out unused space
 * based on findfreeblock()
 */
void zcnzero_main(int argc,char *argv[])
{
static unsigned char avail[256];
unsigned char *drvptr;	/* ptr to drive */
unsigned char *basedirptr; /* ptr to first entry */
unsigned char *dirptr;	/* ptr to current entry */
int nument;	/* num. dir. entries */
int f,g;
int numdatablks;
int drvsiz;
int drv;
int count=0;

if(argc!=1)
  {
  zcntools_main();
  return;
  }

for(drv=0;drv<4;drv++)
  if(drive_valid[drv] && is_zcn_fmt(drv))
    {
    memset(avail,1,256);
    
    drvptr=cardmem+256*1024*drv;
    basedirptr=dirptr=drvptr+1024*(1+drvptr[8]);
    nument=drvptr[9]*32;
    
    drvsiz=drvptr[6]+256*drvptr[7];
    if(drvsiz>256) drvsiz=256;
    numdatablks=drvsiz-1-drvptr[8];
    for(f=0;f<drvptr[9];f++) avail[f]=0;	/* discount dir. blks */
    
    for(f=0;f<nument;f++,dirptr+=32)
      {
      if(dirptr[0]!=0xe5)
        {
        for(g=16;g<32;g++)
          if(dirptr[g]!=0)
            avail[dirptr[g]]=0;
        }
      }
    
    for(f=0;f<numdatablks;f++)
      if(avail[f])
        {
        card_changed=1;
        memset(drvptr+(f+1+drvptr[8])*1024,0,1024);
        count++;
        }
    }

if(count)
  printf("%s: %d blocks zeroed.\n",cmd_name,count);
else
  printf("%s: no blocks to zero.\n",cmd_name);
}


void zcndf_main(int argc,char *argv[])
{
int f,free;

if(argc>1)
  {
  zcntools_main();
  return;
  }

for(f=0;f<4;f++)
  {
  if(drive_valid[f] && drive_formatted[f])
    {
    findfreeblock(f,&free);
    printf("%c:=%dk ",f+'A',free);
    }
  }
printf("\n");
}


void zcnformat_main(int argc,char *argv[])
{
int drive;

if(argc!=2)
  {
  zcntools_main();
  return;
  }

drive=tolower(argv[1][0])-'a';
if(strcmp(argv[1]+1,":")!=0 || drive<0 || drive>3)
  fprintf(stderr,"%s: invalid drive specification.\n",cmd_name),exit(1);

formatdrive(drive);
}


void zcnren_main(int argc,char *argv[])
{
unsigned char *drvptr;	/* ptr to drive */
unsigned char *dirptr;	/* ptr to first entry */
int nument;	/* num. dir. entries */
int f;
char n83_old[12],n83_new[12],*ptr;
int drive=-1,user=-1;
int found;

if(argc!=3)
  {
  zcntools_main();
  return;
  }

ptr=grok_du(argv[1],&drive,&user);
if(*ptr==0)
  fprintf(stderr,"%s: invalid source filename.\n",cmd_name),exit(1);

if(drive==-1) drive=default_drive;
if(user==-1) user=default_user;
strcpy(n83_old,make83(ptr));

strcpy(n83_new,make83(argv[2]));

drvptr=cardmem+256*1024*drive;
dirptr=drvptr+1024*(1+drvptr[8]);
nument=drvptr[9]*32;

/* abort if file exists with new name */
if(bfopen(drive,user,n83_new))
  fprintf(stderr,"%s: file exists.\n",cmd_name),exit(1);

found=0;
for(f=0;f<nument;f++,dirptr+=32)
  {
  if(dirptr[0]==user && memcmp(n83_old,dirptr+1,11)==0)
    found=1,memcpy(dirptr+1,n83_new,11);
  }

if(!found)
  fprintf(stderr,"%s: file not found.\n",cmd_name);
}


/* do findfirst/findnext. Any `*' wildcards must have been converted
 * to `?' before calling.
 * Returns pointer to 11-char FILENAMEEXT, or NULL if no more files match.
 */
char *matchfcb_wild(int drive,int user,char *filespec83,int first)
{
static int first_not_done=1;
static unsigned char *drvptr;	/* ptr to drive */
static unsigned char *dirptr;	/* ptr to first entry */
static int nument;	/* num. dir. entries */
static int f;
int g,bad;

if(first)
  {
  drvptr=cardmem+256*1024*drive;
  dirptr=drvptr+1024*(1+drvptr[8]);
  nument=drvptr[9]*32;
  f=0;
  first_not_done=0;
  }
else
  if(first_not_done)
    fprintf(stderr,
      "matchfcb_wild() findnext without findfirst - can't happen!\n"),
    exit(1);

for(;f<nument;f++,dirptr+=32)
  {
  bad=0;
  if(dirptr[0]==user && dirptr[12]==0)
    {
    for(g=0;g<11;g++)
      if(filespec83[g]!='?' && filespec83[g]!=dirptr[1+g])
        bad=1;
    
    if(!bad)
      {
      /* get ready for next time */
      f++;
      dirptr+=32;
      return(dirptr-32+1);
      }
    }
  }

first_not_done=1;
return(NULL);
}


/* convert `*' in wildcard to `?'. Modifies passed string,
 * returning the pointer.
 */
char *star2ques(char *n83)
{
int f,end;

for(f=0;f<11;f++)
  if(n83[f]=='*')
    {
    end=((f<8)?8:11);
    for(;f<end;f++)
      n83[f]='?';
    f--;	/* defeat the f++ at end of loop */
    }

return(n83);
}


void zcnrm_main(int argc,char *argv[])
{
char n83[12];
char *mfres,*ptr;
int drive=-1,user=-1;

if(argc!=2)
  {
  zcntools_main();
  return;
  }

ptr=grok_du(argv[1],&drive,&user);
if(*ptr==0)
  fprintf(stderr,"%s: invalid filespec.\n",cmd_name),exit(1);
if(drive==-1) drive=default_drive;
if(user==-1) user=default_user;

strcpy(n83,star2ques(make83(ptr)));

mfres=matchfcb_wild(drive,user,n83,1);
if(mfres==NULL)
  fprintf(stderr,"%s: file not found.\n",cmd_name),exit(1);
else
  while(mfres)
    {
    /* the delete failing should be a can't-happen job. */
    if(!bfdel(drive,user,mfres))
      fprintf(stderr,"%s: deletion failed (can't happen?).\n",cmd_name);
    
    mfres=matchfcb_wild(drive,user,n83,0);
    }
}


/* make a sane Unixy filename from a FILENAMEEXT one. */
char *makeunixname(char *n83)
{
static char retname[13];
int f,g;

memset(retname,0,sizeof(retname));

for(f=g=0;f<11;f++)
  {
  if(f==8 && memcmp(n83+8,"   ",3)!=0)
    retname[g++]='.';
  
  switch(n83[f])
    {
    case ' ':	break;
    case '/':	retname[g++]='_'; break;
    default:	retname[g++]=tolower(n83[f]);
    }
  }

return(retname);
}


void zcncat_main(int argc,char *argv[])
{
char n83[12];
char *ptr;
int drive=-1,user=-1;

if(argc!=2)
  {
  zcntools_main();
  return;
  }

ptr=grok_du(argv[1],&drive,&user);
if(*ptr==0)
  fprintf(stderr,"%s: invalid filespec.\n",cmd_name),exit(1);
if(drive==-1) drive=default_drive;
if(user==-1) user=default_user;

strcpy(n83,make83(ptr));

if(!getfile(drive,user,n83,NULL))
  fprintf(stderr,"%s: error catting `%s'.\n",cmd_name,makeunixname(n83));
}


void zcnget_main(int argc,char *argv[])
{
char n83[12];
char *mfres,*ptr,*destname;
int drive=-1,user=-1;

if(argc!=3)
  {
  zcntools_main();
  return;
  }

/* we just do a chdir to test if the dir is ok :-) - it should be ok
 * to simply stay there, too, as nc100.card's path is absolute
 * rather than relative, so writing that won't screw up.
 * (Besides, it won't be written at all, as we're only reading!)
 */
if(chdir(argv[2])==-1)
  fprintf(stderr,"%s: invalid directory.\n",cmd_name),exit(1);

ptr=grok_du(argv[1],&drive,&user);
if(*ptr==0)
  fprintf(stderr,"%s: invalid filespec.\n",cmd_name),exit(1);
if(drive==-1) drive=default_drive;
if(user==-1) user=default_user;

strcpy(n83,star2ques(make83(ptr)));

mfres=matchfcb_wild(drive,user,n83,1);
if(mfres==NULL)
  fprintf(stderr,"%s: file not found.\n",cmd_name),exit(1);
else
  while(mfres)
    {
    destname=makeunixname(mfres);
    if(!getfile(drive,user,mfres,destname))
      fprintf(stderr,"%s: error copying `%s'.\n",cmd_name,destname);
    
    mfres=matchfcb_wild(drive,user,n83,0);
    }
}


void zcnput_main(int argc,char *argv[])
{
char *ptr;
int drive=-1,user=-1;
int f;

if(argc<3)
  {
  zcntools_main();
  return;
  }

ptr=grok_du(argv[argc-1],&drive,&user);
if(*ptr)
  fprintf(stderr,"%s: invalid drive/user specification.\n",cmd_name),exit(1);
if(drive==-1) drive=default_drive;
if(user==-1) user=default_user;

if(!drive_valid[drive] || !drive_formatted[drive])
  {
  fprintf(stderr,"%s: invalid or unformatted drive.\n",cmd_name);
  return;
  }

for(f=1;f<argc-1;f++)
  {
  if(!putfile(argv[f],drive,user))
    fprintf(stderr,"%s: error copying `%s'.\n",cmd_name,argv[f]);
  }
}


void init_globals(char *argv0)
{
char *ptr=strrchr(argv0,'/');

cmd_name=(ptr?ptr+1:argv0);
strcpy(card_file,libdir("nc100.card"));
}


int main(int argc,char *argv[])
{
init_globals(argv[0]);

if(argc==2 && strcmp(argv[1],"-h")==0)
  zcntools_main();
else
  {
  readcard_and_init();
  
  if(strcmp(cmd_name,"zcncat")==0)		zcncat_main(argc,argv);
  else if(strcmp(cmd_name,"zcndf")==0)		zcndf_main(argc,argv);
  else if(strcmp(cmd_name,"zcnformat")==0)	zcnformat_main(argc,argv);
  else if(strcmp(cmd_name,"zcnget")==0)		zcnget_main(argc,argv);
  else if(strcmp(cmd_name,"zcninfo")==0)	zcninfo_main(argc,argv);
  else if(strcmp(cmd_name,"zcnls")==0)		zcnls_main(argc,argv);
  else if(strcmp(cmd_name,"zcnput")==0)		zcnput_main(argc,argv);
  else if(strcmp(cmd_name,"zcnren")==0)		zcnren_main(argc,argv);
  else if(strcmp(cmd_name,"zcnrm")==0)		zcnrm_main(argc,argv);
  else if(strcmp(cmd_name,"zcnzero")==0)	zcnzero_main(argc,argv);
  else zcntools_main();
  
  writecard();
  }
  
exit(0);
}
