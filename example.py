#!/usr/bin/env python3

from gi.repository import Playerctl, GLib

player = Playerctl.Player()


def on_metadata(player, metadata):
    if 'xesam:artist' in metadata.keys() and 'xesam:title' in metadata.keys():
        print('Now playing:')
        print('{artist} - {title}'.format(
            artist=metadata['xesam:artist'][0], title=metadata['xesam:title']))


def on_play(player, status):
    print('Playing at volume {}'.format(player.props.volume))


def on_pause(player, status):
    print('Paused the song: {}'.format(player.get_title()))


player.on('playback-status:playing', on_play)
player.on('playback-status::paused', on_pause)
player.on('metadata', on_metadata)

# start playing some music
player.play()

if player.get_artist() == 'Lana Del Rey':
    player.next()

# wait for events
main = GLib.MainLoop()
main.run()
