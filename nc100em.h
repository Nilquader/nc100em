/* nc100em-specific #define's */

#include "version.h"

/* these are offsets in mem[] */
#define ROM_START	0
#define RAM_START	(512*1024)

extern char *cmd_name;
extern void dontpanic(int a);
extern void dontpanic_nmi(void);
extern void get_mouse_state(int *mxp,int *myp,int *mbutp);
