#!/usr/bin/env python3

from gi.repository import Playerctl, GLib

manager = Playerctl.PlayerManager()


def on_play(player, status, manager):
    print('player is playing: {}'.format(player.props.player_name))


def on_metadata(player, metadata, manager):
    keys = metadata.keys()
    if 'xesam:artist' in keys and 'xesam:title' in keys:
        print('{} - {}'.format(metadata['xesam:artist'][0],
                               metadata['xesam:title']))


def init_player(name):
    # choose if you want to manage the player based on the name
    if name.name in ['vlc', 'cmus']:
        player = Playerctl.Player.new_from_name(name)
        player.connect('playback-status::playing', on_play, manager)
        player.connect('metadata', on_metadata, manager)
        manager.manage_player(player)


def on_name_appeared(manager, name):
    init_player(name)


def on_player_vanished(manager, player):
    print('player has exited: {}'.format(player.props.player_name))


manager.connect('name-appeared', on_name_appeared)
manager.connect('player-vanished', on_player_vanished)

for name in manager.props.player_names:
    init_player(name)

main = GLib.MainLoop()
main.run()
