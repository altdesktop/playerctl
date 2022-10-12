/*
 * This file is part of playerctl.
 *
 * playerctl is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * playerctl is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with playerctl If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright © 2014, Tony Crisci and contributors.
 */

#include "playerctl/playerctl-rc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char **create_empty_options() {
    char **options = (char**)malloc(sizeof(char*) * 2);
    assert(options != NULL);

    // Prepend empty to match argv size
    options[0] = strdup("");
    assert(options[0] != NULL);

    // Null terminated array
    options[1] = NULL;

    return options;
}

static void trim(char **str) {
    // Left trim
    for (; **str == '\r' || **str == '\n' || **str == ' '; (*str)++);

    // Right trim
    for (char *end = *str + strlen(*str) - 1; end >= *str; end--) {
        if (*end == '\r' || *end == '\n' || *end == ' ')
            *end = '\0';
        else
            break;
    }
}

static char **read_rc_file(const char* path) {
    char **options = NULL;
    int options_idx = 0;

    FILE *rc_file;
    if ((rc_file = fopen(path, "r")) != NULL) {
        static int options_cap = 64;
        options = (char**)malloc(sizeof(char*) * options_cap);
        assert(options != NULL);

        // Prepend empty to match argv size
        options[options_idx] = strdup("");
        assert(options[options_idx] != NULL);
        options_idx++;

        static int buf_size = 1024;
        char line[buf_size];
        while (fgets(line, buf_size, rc_file) != NULL && options_idx < options_cap - 1) {
            char* context = NULL;
            char* token = strtok_r(line, " ", &context);
            while (token != NULL) {
                trim(&token);
                if (strlen(token) > 0) {
                    options[options_idx] = strdup(token);
                    assert(options[options_idx] != NULL);

                    if (++options_idx == options_cap - 1)
                        break;
                }
                token = strtok_r(NULL, " ", &context);
            }
        }

        // Null terminated array
        options[options_idx] = NULL;
    }

    return options;
}

char **playerctl_rc_read_options() {
    char **options = NULL;
    char *homedir = getenv("HOME");

    if (homedir != NULL) {
        static char rc_file_name[] = "/.playerctlrc";

        char path[strlen(homedir) + strlen(rc_file_name) + 1];
        sprintf(path, "%s%s", homedir, rc_file_name);

        options = read_rc_file(path);
    }

    if (options == NULL) {
        options = read_rc_file("/etc/playerctlrc");
    }

    if (options == NULL) {
        options = create_empty_options();
    }

    return options;
}
