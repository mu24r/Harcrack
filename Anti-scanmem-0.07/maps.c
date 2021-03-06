/*
 $Id: maps.c,v 1.16 2007-06-05 01:45:35+01 taviso Exp $

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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <stdbool.h>

#include "list.h"
#include "maps.h"
#include "scanmem.h"

bool readmaps(pid_t target, list_t * regions)
{
    FILE *maps;
    char name[32], *line = NULL;
    size_t len = 0;

    /* check if target is valid */
    if (target == 0)
        return false;

    /* construct the maps filename */
    snprintf(name, sizeof(name), "/proc/%u/maps", target);

    /* attempt to open the maps file */
    if ((maps = fopen(name, "r")) == NULL) {
        fprintf(stderr, "error: failed to open maps file %s.\n", name);
        return false;
    }

    eprintf("info: maps file located at %s opened.\n", name);

    /* read every line of the maps file */
    while (getline(&line, &len, maps) != -1) {
        unsigned start, end;
        region_t *map = NULL;
        char read, write, exec, cow, *filename;

        /* slight overallocation */
        if ((filename = alloca(len)) == NULL) {
            fprintf(stderr, "error: failed to allocate %u bytes for filename.\n", len);
            goto error;
        }
        
        /* initialise to zero */
        memset(filename, '\0', len);

        /* parse each line */
        if (sscanf(line, "%x-%x %c%c%c%c %*x %*s %*u %s", &start, &end, &read,
                &write, &exec, &cow, filename) >= 6) {

            /* must have permissions to read and write, and be non-zero size */
            if (write == 'w' && read == 'r' && (end - start) > 0) {

                /* allocate a new region structure */
                if ((map = calloc(1, sizeof(region_t) + strlen(filename))) == NULL) {
                    fprintf(stderr, "error: failed to allocate memory for region.\n");
                    goto error;
                }

                /* initialise this region */
                map->flags.read = true;
                map->flags.write = true;
                map->start = start;
                map->size = (unsigned) (end - start);

                /* setup other permissions */
                map->flags.exec = (exec == 'x');
                map->flags.shared = (cow == 's');
                map->flags.private = (cow == 'p');

                /* save pathname */
                if (strlen(filename) != 0) {
                    /* the pathname is concatenated with the structure */
                    if ((map = realloc(map, sizeof(*map) + strlen(filename))) == NULL) {
                        fprintf(stderr, "error: failed to allocate memory.\n");
                        goto error;
                    }

                    strcpy(map->filename, filename);
                }

                /* add a unique identifier */
                map->id = regions->size;
                
                /* okay, add this guy to our list */
                if (l_append(regions, regions->tail, map) == -1) {
                    fprintf(stderr, "error: failed to save region.\n");
                    goto error;
                }
            }
        }
    }

    eprintf("info: %d suitable regions found.\n", regions->size);
    
    /* release memory allocated */
    free(line);
    fclose(maps);

    return true;

  error:
    free(line);
    fclose(maps);

    return false;
}

int compare_region_id(const region_t *a, const region_t *b)
{    
    return (int) (a->id - b->id);
}

