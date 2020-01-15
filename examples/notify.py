#!/usr/bin/env python3

from gi.repository import GLib
import gi

gi.require_version('Playerctl', '2.0')
from gi.repository import Playerctl

manager = Playerctl.PlayerManager()

gi.require_version('Notify', '0.7')
from gi.repository import Notify

Notify.init("Media Player")
notification = Notify.Notification.new("")

from urllib.parse import urlparse, unquote
from pathlib import Path
from gi.repository import GdkPixbuf
import os.path


def notify(player):
    metadata = player.props.metadata
    keys = metadata.keys()
    if 'xesam:artist' in keys and 'xesam:title' in keys:
        notification.update(metadata['xesam:title'],
                            metadata['xesam:artist'][0])
        path = Path(unquote(urlparse(
            metadata['xesam:url']).path)).parent / "cover.jpg"
        if os.path.exists(path):
            image = GdkPixbuf.Pixbuf.new_from_file(str(path))
            notification.set_image_from_pixbuf(image)
        notification.show()


def on_play(player, status, manager):
    notify(player)


def on_metadata(player, metadata, manager):
    notify(player)


def init_player(name):
    player = Playerctl.Player.new_from_name(name)
    player.connect('playback-status::playing', on_play, manager)
    player.connect('metadata', on_metadata, manager)
    manager.manage_player(player)
    notify(player)


def on_name_appeared(manager, name):
    init_player(name)


manager.connect('name-appeared', on_name_appeared)

for name in manager.props.player_names:
    init_player(name)

main = GLib.MainLoop()
main.run()

Notify.uninit()
