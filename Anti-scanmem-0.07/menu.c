/*
 $Id: menu.c,v 1.15 2007-04-11 10:43:27+01 taviso Exp $

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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "scanmem.h"
#include "commands.h"

static char *commandgenerator(const char *text, int state);
static char **commandcompletion(const char *text, int start, int end);

/*
 * getcommand() reads in a command using readline, and places a pointer to the
 * read string into *line, _which must be free'd by caller_.
 * returns true on success, or false on error.
 */

bool getcommand(globals_t * vars, char **line)
{
    char prompt[64];

    assert(vars != NULL);
    assert(vars->matches != NULL);

    snprintf(prompt, sizeof(prompt), "%u> ", vars->matches->size);

    rl_readline_name = "scanmem";
    rl_attempted_completion_function = commandcompletion;

    while (true) {
        /* read in the next command using readline library */
        if ((*line = readline(prompt)) == NULL) {
            /* EOF */
            if ((*line = strdup("__eof")) == NULL) {
                fprintf(stderr,
                        "error: sorry, there was a memory allocation error.\n");
                return false;
            }
        }

        if (strlen(*line)) {
            break;
        }

        free(*line);
    }

    /* record this line to readline history */
    add_history(*line);
    return true;
}

/* custom completor program for readline */
static char **commandcompletion(const char *text, int start, int end)
{
    (void) end;

    /* never use default completer (filenames), even if I dont generate any matches */
    rl_attempted_completion_over = 1;

    /* only complete on the first word, the command */
    return start ? NULL : rl_completion_matches(text, commandgenerator);
}

/* command generator for readline completion */
static char *commandgenerator(const char *text, int state)
{
    static unsigned index = 0;
    unsigned i;
    size_t len;
    element_t *np;

    /* reset generator if state == 0, otherwise continue from last time */
    index = state ? index : 0;

    np = globals.commands ? globals.commands->head : NULL;

    len = strlen(text);

    /* skip to the last node checked */
    for (i = 0; np && i < index; i++)
        np = np->next;

    /* traverse the commands list, checking for matches */
    while (np) {
        command_t *command = np->data;

        np = np->next;

        /* record progress */
        index++;

        /* if shortdoc is NULL, this is not supposed ot be user visible */
        if (command == NULL || command->command == NULL
            || command->shortdoc == NULL)
            continue;

        /* check if we have a match */
        if (strncmp(text, command->command, len) == 0) {
            return strdup(command->command);
        }
    }

    return NULL;
}

int printversion(FILE * fp)
{
    return fprintf(fp, "scanmem version %s, Copyright (C) 2006-2007 Tavis Ormandy\n"
                       "scanmem comes with ABSOLUTELY NO WARRANTY; for details type `show warranty'.\n"
                       "This is free software, and you are welcome to redistribute it\n"
                       "under certain conditions; type `show copying' for details.\n\n", PACKAGE_VERSION);
}
