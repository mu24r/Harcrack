/*

 $Id: scanmem.h,v 1.21 2007-06-05 01:45:35+01 taviso Exp $

*/

#ifndef _SCANMEM_INC
#define _SCANMEM_INC            /* include guard */

#include <stdint.h>
#include <sys/types.h>          /*lint !e537 */

#include "list.h"
#include "value.h"
#include "maps.h"

/*lint +libh(config.h) */

#include "config.h"

/* list of functions where i dont want to be warned about ignored return value */

/*lint -esym(534,detach,printversion,strftime,fflush,sleep) */

#ifndef PACKAGE_VERSION
# define  PACKAGE_VERSION "(unknown)"
#endif

#ifndef NDEBUG
# define eprintf(x, y...) fprintf(stderr, x, ## y)
#else
# define eprintf(x, y...)
#endif

#ifdef _lint
/*lint -save -e652 -e683 -e547 */
# define snprintf(a, b, c...) (((void) b), sprintf(a, ## c))
# define strtoll(a,b,c) ((long long) strtol(a,b,c))
# define WIFSTOPPED
# define sighandler_t _sigfunc_t
/*lint -restore */
/*lint -save -esym(526,getline,strdupa,strdup,strndupa,strtoll,pread) */
ssize_t getline(char **lineptr, size_t * n, FILE * stream);
char *strndupa(const char *s, size_t n);
char *strdupa(const char *s);
char *strdup(const char *s);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
/*lint -restore */
#endif
#ifdef __CSURF__
# define waitpid(x,y,z) ((*(y)=0),-rand())
# define WIFSTOPPED(x) (rand())
# define ptrace(w,x,y,z) ((errno=rand()),(ptrace(w,x,y,z)))
#endif
#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

/* global settings */
typedef struct {
    unsigned exit:1;
    pid_t target;
    list_t *matches;
    list_t *regions;
    list_t *commands;
} globals_t;

/* this structure represents one known match, its address and type. */
typedef struct {
    void *address;              /* address of variable */
    region_t *region;           /* region it belongs to */
    value_t lvalue;             /* last seen value */
    unsigned matchid;           /* unique identifier */
} match_t;


/* global settings */
extern globals_t globals;

bool detach(pid_t target);
bool setaddr(pid_t target, void *addr, const value_t * to);
bool checkmatches(list_t * matches, pid_t target, value_t value,
                  matchtype_t type);
bool searchregions(list_t * matches, const list_t * regions, pid_t target,
                value_t value, bool snapshot);
bool peekdata(pid_t pid, void *addr, value_t * result);
bool attach(pid_t target);
bool getcommand(globals_t * vars, char **line);
int printversion(FILE * fp);
#endif
