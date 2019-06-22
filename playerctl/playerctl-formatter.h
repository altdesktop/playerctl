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
 * Copyright © 2014, Tony Crisci and contributors
 */

#ifndef __PLAYERCTL_FORMATTER_H__
#define __PLAYERCTL_FORMATTER_H__

#include <glib.h>
#include <playerctl/playerctl.h>

typedef struct _PlayerctlFormatter PlayerctlFormatter;
typedef struct _PlayerctlFormatterPrivate PlayerctlFormatterPrivate;

struct _PlayerctlFormatter {
    PlayerctlFormatterPrivate *priv;
};

PlayerctlFormatter *playerctl_formatter_new(const gchar *format, GError **error);

void playerctl_formatter_destroy(PlayerctlFormatter *formatter);

gboolean playerctl_formatter_contains_key(PlayerctlFormatter *formatter, const gchar *key);

GVariantDict *playerctl_formatter_default_template_context(PlayerctlFormatter *formatter,
                                                           PlayerctlPlayer *player, GVariant *base);

gchar *playerctl_formatter_expand_format(PlayerctlFormatter *formatter, GVariantDict *context,
                                         GError **error);

#endif /* __PLAYERCTL_FORMATTER_H__ */
