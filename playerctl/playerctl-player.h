/* vim:ts=2:sw=2:expandtab
 *
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
 * Copyright © 2014, Tony Crisci
 */

#ifndef __PLAYERCTL_PLAYER_H__
#define __PLAYERCTL_PLAYER_H__

#if !defined(__PLAYERCTL_INSIDE__) && !defined(PLAYERCTL_COMPILATION)
#error "Only <playerctl/playerctl.h> can be included directly."
#endif

#include <glib-object.h>
/**
  * SECTION: playerctl-player
  * @short_description: A class to control an MPRIS player
  *
  */

#define PLAYERCTL_TYPE_PLAYER                  (playerctl_player_get_type ())
#define PLAYERCTL_PLAYER(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), PLAYERCTL_TYPE_PLAYER, PlayerctlPlayer))
#define PLAYERCTL_IS_PLAYER(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PLAYERCTL_TYPE_PLAYER))
#define PLAYERCTL_PLAYER_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), PLAYERCTL_TYPE_PLAYER, PlayerctlPlayerClass))
#define PLAYERCTL_IS_PLAYER_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), PLAYERCTL_TYPE_PLAYER))
#define PLAYERCTL_PLAYER_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), PLAYERCTL_TYPE_PLAYER, PlayerctlPlayerClass))

typedef struct _PlayerctlPlayer        PlayerctlPlayer;
typedef struct _PlayerctlPlayerClass   PlayerctlPlayerClass;
typedef struct _PlayerctlPlayerPrivate PlayerctlPlayerPrivate;

struct _PlayerctlPlayer
{
  /* Parent instance structure */
  GObject parent_instance;

  /* private member */
  PlayerctlPlayerPrivate *priv;

  /* instance members */
};

struct _PlayerctlPlayerClass
{
  /* Parent class structure */
  GObjectClass parent_class;

  /* class members */
};

GType playerctl_player_get_type (void);

PlayerctlPlayer * playerctl_player_new (gchar *name, GError **err);

/*
 * Method definitions.
 */

PlayerctlPlayer *playerctl_player_on(PlayerctlPlayer *self, const gchar *event, GClosure *callback, GError **err);

PlayerctlPlayer *playerctl_player_play_pause(PlayerctlPlayer *self, GError **err);

PlayerctlPlayer *playerctl_player_play(PlayerctlPlayer *self, GError **err);

PlayerctlPlayer *playerctl_player_stop(PlayerctlPlayer *self, GError **err);

PlayerctlPlayer *playerctl_player_pause(PlayerctlPlayer *self, GError **err);

PlayerctlPlayer *playerctl_player_next(PlayerctlPlayer *self, GError **err);

PlayerctlPlayer *playerctl_player_previous(PlayerctlPlayer *self, GError **err);

gchar *playerctl_player_print_metadata_prop(PlayerctlPlayer *self, gchar *property, GError **err);

gchar *playerctl_player_get_artist(PlayerctlPlayer *self, GError **err);

gchar *playerctl_player_get_title(PlayerctlPlayer *self, GError **err);

gchar *playerctl_player_get_album(PlayerctlPlayer *self, GError **err);

#endif /* __PLAYERCTL_PLAYER_H__ */
