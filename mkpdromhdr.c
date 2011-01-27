#include <stdio.h>

int main(void)
{
static unsigned char buf[4096];		/* we know it's pretty small */
int f;
int size=fread(buf,1,sizeof(buf),stdin);

printf("/* PD ROM header file - automatically generated from pdrom.bin,\n"
	" * edits will be lost!\n"
	" */\n\n"
	"unsigned char pd_rom[]=\n  {");

for(f=0;f<size;f++)
  {
  if((f%8)==0) printf("\n  ");
  printf("0x%02x,",buf[f]);
  }

printf("\n  };\n");
exit(0);
}
