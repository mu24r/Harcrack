/*
 $Id: handlers.c,v 1.12 2007-06-05 01:45:34+01 taviso Exp $

 Copyright (C) 2006,2007 Tavis Ormandy <taviso@sdf.lonestar.org>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <assert.h>
#include <setjmp.h>
#include <alloca.h>
#include <strings.h>            /*lint -esym(526,strcasecmp) */
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

#include <readline/readline.h>

#include "scanmem.h"
#include "commands.h"
#include "handlers.h"
#include "interrupt.h"

#define USEPARAMS() ((void) vars, (void) argv, (void) argc)     /* macro to hide gcc unused warnings */

/*lint -esym(818, vars, argv) dont want want to declare these as const */

/*
 * This file defines all the command handlers used, each one is registered using
 * registercommand(), and when a matching command is entered, the commandline is
 * tokenized and parsed into an argv/argc.
 * 
 * argv[0] will contain the command entered, so one handler can handle multiple
 * commands by checking whats in there, but you still need seperate documentation
 * for each command when you register it.
 *
 * Most commands will also need some documentation, see handlers.h for the format.
 *
 * Commands are allowed to read and modify settings in the vars structure.
 *
 */

#define calloca(x,y) (memset(alloca((x) * (y)), 0x00, (x) * (y)))

bool handler__set(globals_t * vars, char **argv, unsigned argc)
{
    unsigned block, seconds = 1;
    char *delay = NULL;
    bool cont = false;
    struct setting {
        char *matchids;
        char *value;
        unsigned seconds;
    } *settings = NULL;

    assert(argc != 0);
    assert(argv != NULL);
    assert(vars != NULL);

    if (argc < 2) {
        fprintf(stderr,
                "error: expected an argument, type `help set` for details.\n");
        return false;
    }

    /* check if there are any matches */
    if (vars->matches->size == 0) {
        fprintf(stderr, "error: no matches are known yet.\n");
        return false;
    }

    /* --- parse arguments into settings structs --- */

    settings = calloca(argc - 1, sizeof(struct setting));

    /* parse every block into a settings struct */
    for (block = 0; block < argc - 1; block++) {

        /* first seperate the block into matches and value, which are separated by '=' */
        if ((settings[block].value = strchr(argv[block + 1], '=')) == NULL) {

            /* no '=' found, whole string must be the value */
            settings[block].value = argv[block + 1];
        } else {
            /* there is a '=', value+1 points to value string. */

            /* use strndupa() to copy the matchids into a new buffer */
            settings[block].matchids =
                strndupa(argv[block + 1],
                         (size_t) (settings[block].value++ - argv[block + 1]));
        }

        /* value points to the value string, possibly with a delay suffix */

        /* matchids points to the match-ids (possibly multiple) or NULL */

        /* now check for a delay suffix (meaning continuous mode), eg 0xff/10 */
        if ((delay = strchr(settings[block].value, '/')) != NULL) {
            char *end = NULL;

            /* parse delay count */
            settings[block].seconds = strtoul(delay + 1, &end, 10);

            if (*(delay + 1) == '\0') {
                /* empty delay count, eg: 12=32/ */
                fprintf(stderr,
                        "error: you specified an empty delay count, `%s`, see `help set`.\n",
                        settings[block].value);
                return false;
            } else if (*end != '\0') {
                /* parse failed before end, probably trailing garbage, eg 34=9/16foo */
                fprintf(stderr,
                        "error: trailing garbage after delay count, `%s`.\n",
                        settings[block].value);
                return false;
            } else if (settings[block].seconds == 0) {
                /* 10=24/0 disables continous mode */
                fprintf(stderr,
                        "info: you specified a zero delay, disabling continuous mode.\n");
            } else {
                /* valid delay count seen and understood */
                fprintf(stderr,
                        "info: setting %s every %u seconds until interrupted...\n",
                        settings[block].matchids ? settings[block].
                        matchids : "all", settings[block].seconds);

                /* continuous mode on */
                cont = true;
            }

            /* remove any delay suffix from the value */
            settings[block].value =
                strndupa(settings[block].value,
                         (size_t) (delay - settings[block].value));
        }                       /* if (strchr('/')) */
    }                           /* for(block...) */

    /* --- setup a longjmp to handle interrupt --- */
    if (INTERRUPTABLE()) {
        
        /* control returns here when interrupted */
        detach(vars->target);
        ENDINTERRUPTABLE();
        return true;
    }

    /* --- execute the parsed setting structs --- */

    while (true) {
        unsigned i;
        value_t val;
        char *end = NULL;
        element_t *np = vars->matches->head;

        /* for every settings struct */
        for (block = 0; block < argc - 1; block++) {

            /* reset linked list ptr */
            np = vars->matches->head;

            /* check if this block has anything to do this iteration */
            if (seconds != 1) {
                /* not the first iteration (all blocks get executed first iteration) */

                /* if settings.seconds is zero, then this block is only executed once */
                /* if seconds % settings.seconds is zero, then this block must be executed */
                if (settings[block].seconds == 0
                    || (seconds % settings[block].seconds) != 0)
                    continue;
            }

            /* convert value */
            strtoval(settings[block].value, &end, 0x00, &val);

            /* check that converted successfully */
            if (*end != '\0') {
                fprintf(stderr, "error: could not parse value `%s`\n",
                        settings[block].value);
                goto fail;
            } else if (*settings[block].value == '\0') {
                fprintf(stderr, "error: you didnt specify a value.\n");
                goto fail;
            }

            /* check if specific match(s) were specified */
            if (settings[block].matchids != NULL) {
                char *id, *lmatches = NULL;
                unsigned num = 0;

                /* create local copy of the matchids for strtok() to modify */
                lmatches = strdupa(settings[block].matchids);

                /* now seperate each match, spearated by commas */
                while ((id = strtok(lmatches, ",")) != NULL) {
                    match_t *match;

                    /* set to NULL for strtok() */
                    lmatches = NULL;

                    /* parse this id */
                    num = strtoul(id, &end, 0x00);

                    /* check that succeeded */
                    if (*id == '\0' || *end != '\0') {
                        fprintf(stderr,
                                "error: could not parse match id `%s`\n", id);
                        goto fail;
                    }

                    /* check this is a valid match-id */
                    if (num < vars->matches->size) {
                        value_t v;

                        /*lint -e722 semi-colon intended, skip to correct node */
                        for (i = 0, np = vars->matches->head; i < num;
                             i++, np = np->next);

                        match = np->data;

                        fprintf(stderr, "info: setting *%p to %#x...\n",
                                match->address, val.value.tuint);

                        /* copy val onto v */
                        valcpy(&v, &val);

                        /* XXX: valcmp? make sure the sizes match */
                        truncval(&v, &match->lvalue);

                        /* set the value specified */
                        if (setaddr(vars->target, match->address, &v) == false) {
                            fprintf(stderr, "error: failed to set a value.\n");
                            goto fail;
                        }

                    } else {
                        /* match-id > than number of matches */
                        fprintf(stderr,
                                "error: found an invalid match-id `%s`\n", id);
                        goto fail;
                    }
                }
            } else {

                /* user wants to set all matches */
                while (np) {
                    match_t *match = np->data;

                    /* XXX: as above : make sure the sizes match */
                    truncval(&val, &match->lvalue);

                    fprintf(stderr, "info: setting *%p to %#x...\n",
                            match->address, val.value.tuint);


                    if (setaddr(vars->target, match->address, &val) == false) {
                        fprintf(stderr, "error: failed to set a value.\n");
                        goto fail;
                    }

                    np = np->next;
                }
            }                   /* if (matchid != NULL) else ... */
        }                       /* for(block) */

        if (cont) {
            sleep(1);
        } else {
            break;
        }

        seconds++;
    }                           /* while(true) */

    ENDINTERRUPTABLE();
    return true;

fail:
    ENDINTERRUPTABLE();
    return false;
    
}

/* XXX: add yesno command to check if matches > 099999 */
/* example: [012] 0xffffff, csLfznu, 120, /lib/libc.so */

bool handler__list(globals_t * vars, char **argv, unsigned argc)
{
    unsigned i = 0;

    USEPARAMS();

    element_t *np = vars->matches->head;

    /* list all known matches */
    while (np) {
        char v[32];
        match_t *match = np->data;

        if (valtostr(&match->lvalue, v, sizeof(v)) != true) {
            strncpy(v, "unknown", sizeof(v));
        }

        fprintf(stdout, "[%2u] %10p, %s, %s\n", i++, match->address, v,
                match->region->filename[0] ? match->region->filename : "unassociated, typically .bss");
        np = np->next;
    }

    return true;
}

/* XXX: handle multiple deletes, eg delete !1 2 3 4 5 6
   rememvber to check the id-s work correctly, and deleteing one doesnt fux it up.
*/
bool handler__delete(globals_t * vars, char **argv, unsigned argc)
{
    unsigned id;
    char *end = NULL;

    if (argc != 2) {
        fprintf(stderr,
                "error: was expecting one argument, see `help delete`.\n");
        return false;
    }

    /* parse argument */
    id = strtoul(argv[1], &end, 0x00);

    /* check that strtoul() worked */
    if (argv[1][0] == '\0' || *end != '\0') {
        fprintf(stderr, "error: sorry, couldnt parse `%s`, try `help delete`\n",
                argv[1]);
        return false;
    }

    /* check this is a valid match-id */
    if (id >= vars->matches->size) {
        fprintf(stderr, "warn: you specified a non-existant match `%u`.\n", id);
        fprintf(stderr, "info: use \"list\" to list matches, or \"help\" for other commands.\n");
        return false;
    }

    l_remove_nth(vars->matches, id - 1, NULL);

    return true;
}

bool handler__reset(globals_t * vars, char **argv, unsigned argc)
{

    USEPARAMS();

    l_destroy(vars->matches);

    if ((vars->matches = l_init()) == NULL) {
        fprintf(stderr, "error: sorry, there was a memory allocation error.\n");
        return false;
    }

    /* refresh list of regions */
    l_destroy(vars->regions);

    /* create a new linked list of regions */
    if ((vars->regions = l_init()) == NULL) {
        fprintf(stderr,
                "error: sorry, there was a problem allocating memory.\n");
        return false;
    }

    /* read in maps if a pid is known */
    if (vars->target && readmaps(vars->target, vars->regions) != true) {
        fprintf(stderr,
                "error: sorry, there was a problem getting a list of regions to search.\n");
        fprintf(stderr,
                "warn: the pid may be invalid, or you dont have permission.\n");
        vars->target = 0;
        return false;
    }

    return true;
}

bool handler__pid(globals_t * vars, char **argv, unsigned argc)
{
    char *resetargv[] = { "reset", NULL };
    char *end = NULL;

    if (argc == 2) {
        vars->target = (pid_t) strtoul(argv[1], &end, 0x00);

        if (vars->target == 0) {
            fprintf(stderr, "error: `%s` does not look like a valid pid.\n",
                    argv[1]);
            return false;
        }
    } else if (vars->target) {
        /* print the pid of the target program */
        fprintf(stderr, "info: target pid is %u.\n", vars->target);
        return true;
    } else {
        fprintf(stderr, "info: no target is currently set.\n");
        return false;
    }

    return handler__reset(vars, resetargv, 1);
}

bool handler__snapshot(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();
    value_t v;
    
    /* unused */
    v.value.tslong = -1;

    /* check that a pid has been specified */
    if (vars->target == 0) {
        fprintf(stderr, "error: no target set, type `help pid`.\n");
        return false;
    }

    /* remove any existing matches */
    l_destroy(vars->matches);

    /* allocate new matches list */
    if ((vars->matches = l_init()) == NULL) {
        fprintf(stderr, "error: sorry, there was a memory allocation error.\n");
        return false;
    }

    if (searchregions(vars->matches, vars->regions, vars->target, v, true) != true) {
        fprintf(stderr, "error: failed to save target address space.\n");
        return false;
    }

    return true;
}

/* dregion [!][x][,x,...] */
bool handler__dregion(globals_t * vars, char **argv, unsigned argc)
{
    unsigned id;
    bool invert = false;
    char *end = NULL, *idstr = NULL, *block = NULL;
    element_t *np, *t, *p, *pp;
    list_t *keep = NULL;
    region_t *save;

    /* need an argument */
    if (argc < 2) {
        fprintf(stderr, "error: expected an argument, see `help dregion`.\n");
        return false;
    }

     /* check that there is a process known */
    if (vars->target == 0) {
        fprintf(stderr, "error: no target specified, see `help pid`\n");
        return false;
    }
    
    /* check for an inverted match */
    if (*argv[1] == '!') {
        invert = true;
        /* create a copy of the argument for strtok(), +1 to skip '!' */
        block = strdupa(argv[1] + 1);
        
        /* check for lone '!' */
        if (*block == '\0') {
            fprintf(stderr, "error: inverting an empty set, maybe try `reset` instead?\n");
            return false;
        }
        
        /* create a list to keep the specified regions */
        if ((keep = l_init()) == NULL) {
            fprintf(stderr, "error: memory allocation error.\n");
            return false;
        }
        
    } else {
        invert = false;
        block = strdupa(argv[1]);
    }

    /* loop for every number specified, eg "1,2,3,4,5" */
    while ((idstr = strtok(block, ",")) != NULL) {
        region_t *r = NULL;
        
        /* set block to NULL for strtok() */
        block = NULL;
        
        /* attempt to parse as a regionid */
        id = strtoul(idstr, &end, 0x00);

        /* check that worked, "1,abc,4,,5,6foo" */
        if (*end != '\0' || *idstr == '\0') {
            fprintf(stderr, "error: could not parse argument %s.\n", idstr);
            if (invert) {
                if (l_concat(vars->regions, &keep) == -1) {
                    fprintf(stderr, "error: there was a problem restoring saved regions.\n");
                    l_destroy(vars->regions);
                    l_destroy(keep);
                    return false;
                }
            }
            assert(keep == NULL);
            return false;
        }
        
        /* initialise list pointers */
        np = vars->regions->head;
        t = vars->matches->head;
        p = pp = NULL;
        
        /* find the correct region node */
        while (np) {
            r = np->data;
            
            /* compare the node id to the id the user specified */
            if (r->id == id)
                break;
            
            pp = np; /* keep track of prev for l_remove() */
            np = np->next;
        }

        /* check if a match was found */
        if (np == NULL) {
            fprintf(stderr, "error: no region matching %u, or already moved.\n", id);
            if (invert) {
                if (l_concat(vars->regions, &keep) == -1) {
                    fprintf(stderr, "error: there was a problem restoring saved regions.\n");
                    l_destroy(vars->regions);
                    l_destroy(keep);
                    return false;
                }
            }
            if (keep)
                l_destroy(keep);
            return false;
        }
        
        np = pp;
        
        /* save this region if the match is inverted */
        if (invert) {
            
            assert(keep != NULL);
            
            l_remove(vars->regions, np, (void *) &save);
            if (l_append(keep, keep->tail, save) == -1) {
                fprintf(stderr, "error: sorry, there was an internal memory error.\n");
                free(save);
                return false;
            }
            continue;
        }
        
        /* check for any affected matches before removing it */
        while (t) {
            match_t *match = t->data;
            region_t *s;

            /* determine the correct pointer we're supposed to be checking */
            if (np) {
                assert(np->next);
                s = np->next->data;
            } else {
                /* head of list */
                s = vars->regions->head->data;
            }

            /* check if this one should go */
            if (match->region == s) {
                /* remove this match */
                l_remove(vars->matches, p, NULL);

                /* move to next element */
                t = p ? p->next : vars->matches->head;
            } else {
                p = t;
                t = t->next;
            }
        }

        l_remove(vars->regions, np, NULL);
    }

    if (invert) {
        element_t *nrp = vars->regions->head, *pmp, *nmp;

        while(nrp) {
            region_t *region = nrp->data;
            
            nmp = vars->matches->head;
            pmp = NULL;
            
            while (nmp) {
                match_t *match = nmp->data;

             	 /* check if this one should go */
                if (match->region->id == region->id) {
					     /* remove this match */
                    l_remove(vars->matches, pmp, NULL); 
                   
                    nmp = pmp ? pmp->next : vars->matches->head;
                } else {
                    pmp = nmp;
                    nmp = nmp->next;
                }
            }
            
            nrp = nrp->next;
        }
        
        /* okay, done with the regions list */
        l_destroy(vars->regions);
        
        /* and switch to the keep list */
        vars->regions = keep;
    }

    return true;
}

bool handler__lregions(globals_t * vars, char **argv, unsigned argc)
{
    element_t *np = vars->regions->head;

    USEPARAMS();

    if (vars->target == 0) {
        fprintf(stderr, "error: no target has been specified, see `help pid`.\n");
        return false;
    }

    if (vars->regions->size == 0) {
        fprintf(stdout, "info: no regions are known.\n");
    }
    
    /* print a list of regions that are searched */
    while (np) {
        region_t *region = np->data;

        fprintf(stderr, "[%2u] %#10x, %7u bytes, %c%c%c, %s\n",
                region->id, region->start, region->size,
                region->flags.read ? 'r' : '-',
                region->flags.write ? 'w' : '-',
                region->flags.exec ? 'x' : '-',
                region->filename[0] ? region->filename : "unassociated");
        np = np->next;
    }

    return true;
}

bool handler__decinc(globals_t * vars, char **argv, unsigned argc)
{
    value_t val;
    matchtype_t m;

    USEPARAMS();

    memset(&val, 0x00, sizeof(val));

    switch (argv[0][0]) {
    case '=':
        m = MATCHEQUAL;
        break;
    case '<':
        m = MATCHLESSTHAN;
        break;
    case '>':
        m = MATCHGREATERTHAN;
        break;
    default:
        fprintf(stderr,
                "error: unrecogised match type seen at decinc handler.\n");
        return false;
    }

    /* the last seen value is still there */
    if (vars->matches->size) {
        if (checkmatches(vars->matches, vars->target, val, m) == false) {
            fprintf(stderr, "error: failed to search target address space.\n");
            return false;
        }
    } else {
        fprintf(stderr, "error: cannot use that search without matches\n");
        return false;
    }

    if (vars->matches->size == 1) {
        fprintf(stderr,
                "info: match identified, use \"set\" to modify value.\n");
        fprintf(stderr, "info: enter \"help\" for other commands.\n");
    }

    return true;
}

bool handler__version(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();

    printversion(stdout);
    return true;
}

bool handler__default(globals_t * vars, char **argv, unsigned argc)
{
    char *end = NULL;
    value_t val;

    USEPARAMS();

    /* attempt to parse command as a number */
    strtoval(argv[0], &end, 0x00, &val);

    /* check if that worked */
    if (*end != '\0') {
        fprintf(stderr,
                "error: unable to parse command `%s`, gave up at `%s`\n",
                argv[0], end);
        return false;
    }

    /* need a pid for the rest of this to work */
    if (vars->target == 0) {
        return false;
    }

    /* user has specified an exact value of the variable to find */
    if (vars->matches->size) {
        /* already know some matches */
        if (checkmatches(vars->matches, vars->target, val, MATCHEXACT) != true) {
            fprintf(stderr, "error: failed to search target address space.\n");
            return false;
        }
    } else {
        /* initial search */
        if (searchregions(vars->matches, vars->regions, vars->target, val, false) != true) {
            fprintf(stderr, "error: failed to search target address space.\n");
            return false;
        }
    }

    /* check if we now know the only possible candidate */
    if (vars->matches->size == 1) {
        fprintf(stderr,
                "info: match identified, use \"set\" to modify value.\n");
        fprintf(stderr, "info: enter \"help\" for other commands.\n");
    }

    return true;
}

bool handler__exit(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();

    vars->exit = 1;
    return true;
}

#define DOC_COLUMN 11           /* which column descriptions start on with help command */

bool handler__help(globals_t * vars, char **argv, unsigned argc)
{
    list_t *commands = vars->commands;
    element_t *np = NULL;
    command_t *def = NULL;
    assert(commands != NULL);
    assert(argc >= 1);

    np = commands->head;

    /* print version information for generic help */
    if (argv[1] == NULL)
        printversion(stdout);

    /* traverse the commands list, printing out the relevant documentation */
    while (np) {
        command_t *command = np->data;

        /* remember the default command */
        if (command->command == NULL)
            def = command;

        /* just `help` with no argument */
        if (argv[1] == NULL) {
            int width;

            /* NULL shortdoc means dont print in help listing */
            if (command->shortdoc == NULL) {
                np = np->next;
                continue;
            }

            /* print out command name */
            if ((width =
                 fprintf(stdout, "%s",
                         command->command ? command->command : "default")) <
                0) {
                /* hmm? */
                np = np->next;
                continue;
            }

            /* print out the shortdoc description */
            fprintf(stdout, "%*s%s\n", DOC_COLUMN - width, "",
                    command->shortdoc);

            /* detailed information requested about specific command */
        } else if (command->command
                   && strcasecmp(argv[1], command->command) == 0) {
            fprintf(stdout, "%s\n",
                    command->longdoc ? command->
                    longdoc : "missing documentation");
            return true;
        }

        np = np->next;
    }

    if (argc > 1) {
        fprintf(stderr, "error: unknown command `%s`\n", argv[1]);
        return false;
    } else if (def) {
        fprintf(stdout, "\n%s\n", def->longdoc ? def->longdoc : "");
    }

    return true;
}

bool handler__eof(globals_t * vars, char **argv, unsigned argc)
{
    fprintf(stdout, "exit\n");
    return handler__exit(vars, argv, argc);
}

/* XXX: handle !ls style escapes */
bool handler__shell(globals_t * vars, char **argv, unsigned argc)
{
    size_t len = argc;
    unsigned i;
    char *command;

    USEPARAMS();

    if (argc < 2) {
        fprintf(stderr,
                "error: shell command requires an argument, see `help shell`.\n");
        return false;
    }

    /* convert arg vector into single string, first calculate length */
    for (i = 1; i < argc; i++)
        len += strlen(argv[i]);

    /* allocate space */
    command = calloca(len, 1);

    /* concatenate strings */
    for (i = 1; i < argc; i++) {
        strcat(command, argv[i]);
        strcat(command, " ");
    }

    /* finally execute command */
    if (system(command) == -1) {
        fprintf(stderr, "error: system() failed, command was not executed.\n");
        return false;
    }

    return true;
}

bool handler__watch(globals_t * vars, char **argv, unsigned argc)
{
    value_t o, n;
    unsigned i, id;
    element_t *np = vars->matches->head;
    match_t *match;
    char *end = NULL, buf[64], timestamp[64];
    time_t t;

    if (argc != 2) {
        fprintf(stderr,
                "error: was expecting one argument, see `help watch`.\n");
        return false;
    }

    /* parse argument */
    id = strtoul(argv[1], &end, 0x00);

    /* check that strtoul() worked */
    if (argv[1][0] == '\0' || *end != '\0') {
        fprintf(stderr, "error: sorry, couldnt parse `%s`, try `help watch`\n",
                argv[1]);
        return false;
    }

    /* check this is a valid match-id */
    if (id >= vars->matches->size) {
        fprintf(stderr, "error: you specified a non-existant match `%u`.\n",
                id);
        fprintf(stderr,
                "info: use \"list\" to list matches, or \"help\" for other commands.\n");
        return false;
    }

    /*lint -e722 skip to the correct node, semi-colon intended */
    for (i = 0; np && i < id; i++, np = np->next);

    if (np == NULL) {
        fprintf(stderr, "error: couldnt locate match `%u`.\n", id);
        return false;
    }

    match = np->data;

    valcpy(&o, &match->lvalue);
    valcpy(&n, &o);

    if (valtostr(&o, buf, sizeof(buf)) == false) {
        strncpy(buf, "unknown", sizeof(buf));
    }

    if (INTERRUPTABLE()) {
        (void) detach(vars->target);
        ENDINTERRUPTABLE();
        return true;
    }

    /* every entry is timestamped */
    t = time(NULL);
    strftime(timestamp, sizeof(timestamp), "[%T]", localtime(&t));

    fprintf(stdout,
            "info: %s monitoring %10p for changes until interrupted...\n",
            timestamp, match->address);

    while (true) {

        if (attach(vars->target) == false)
            return false;

        if (peekdata(vars->target, match->address, &n) == false)
            return false;

        detach(vars->target);

        truncval(&n, &match->lvalue);

        /* check if the new value is different */
        if (valuecmp(&o, MATCHNOTEQUAL, &n, NULL)) {

            valcpy(&o, &n);
            truncval(&o, &match->lvalue);

            if (valtostr(&o, buf, sizeof(buf)) == false) {
                strncpy(buf, "unknown", sizeof(buf));
            }

            /* fetch new timestamp */
            t = time(NULL);
            strftime(timestamp, sizeof(timestamp), "[%T]", localtime(&t));

            fprintf(stdout, "info: %s %10p -> %s\n", timestamp, match->address,
                    buf);
        }

        (void) sleep(1);
    }
}

#include "licence.h"

bool handler__show(globals_t * vars, char **argv, unsigned argc)
{
    USEPARAMS();
    
    if (argv[1] == NULL) {
        fprintf(stderr, "error: expecting an argument.\n");
        return false;
    }
    
    if (strcmp(argv[1], "copying") == 0)
        fprintf(stdout, SM_COPYING);
    else if (strcmp(argv[1], "warranty") == 0)
        fprintf(stdout, SM_WARRANTY);
    else if (strcmp(argv[1], "version") == 0)
        printversion(stdout);
    else {
        fprintf(stderr, "error: unrecognised show command `%s`\n", argv[1]);
        return false;
    }
    
    return true;
}
