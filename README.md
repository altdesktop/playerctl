# Playerctl

For true players only: spotify, vlc, audacious, bmp, xmms2, mplayer, and others.

## About

Playerctl is a command-line utility and library for controlling media players that implement the [MPRIS](http://specifications.freedesktop.org/mpris-spec/latest/) D-Bus Interface Specification. Playerctl makes it easy to bind player actions, such as play and pause, to media keys.

For more advanced users, Playerctl provides an [introspectable](https://wiki.gnome.org/action/show/Projects/GObjectIntrospection) library available in your favorite scripting language that allows more detailed control like the ability to subscribe to media player events or get metadata such as artist and title for the playing track.

For examples, use cases, and project goals, see my [blog post](http://dubstepdish.com/blog/2014/04/19/introducing-playerctl/).

## Installing

First, check and see if the library is available from your package manager (if it is not, get someone to host a package for you).

Using the library requires [GLib](https://developer.gnome.org/glib/) (which is a dependency of almost all of these players as well, so you probably already have it).

To generate and build the project:

```shell
./autogen.sh # --prefix=/usr may be required
make
sudo make install
```

## Using the CLI

The `playerctl` binary should now be in your path.

```
playerctl [--version] [--player=NAME] COMMAND
```

Pass the name of your player as the first argument (`playerctl` will attempt to connect to `org.mpris.MediaPlayer2.[NAME]` on your DBus session). Otherwise it will use the first player it can find.

Here is a list of available commands:

```
  play            Command the player to play
  pause           Command the player to pause
  play-pause      Command the player to toggle between play/pause
  next            Command the player to skip to the next track
  previous        Command the player to skip to the previous track
  volume [LEVEL]  Print or set the volume to LEVEL from 0.0 to 1.0
  status          Get the play status of the player
  metadata [KEY]  Print metadata information for the current track. Print only value of KEY if passed.
```

## Using the Library

The latest documentation for the library is available [here](http://dubstepdish.com/playerctl).

To use a scripting library, find your favorite language from [this list](https://wiki.gnome.org/Projects/GObjectIntrospection/Users) and install the bindings library.

### Example Python Script

This example uses the [Python bindings](https://wiki.gnome.org/action/show/Projects/PyGObject).

```python
#!/usr/bin/env python3

from gi.repository import Playerctl, GLib

player = Playerctl.Player()

def on_metadata(player, e):
    if 'xesam:artist' in e.keys() and 'xesam:title' in e.keys():
        print('Now playing:')
        print('{artist} - {title}'.format(artist=e['xesam:artist'][0], title=e['xesam:title']))

def on_play(player):
    print('Playing at volume {}'.format(player.props.volume))

def on_pause(player):
    print('Paused the song: {}'.format(player.get_title()))

player.on('play', on_play)
player.on('pause', on_pause)
player.on('metadata', on_metadata)

# start playing some music
player.play()

if player.get_artist() == 'Lana Del Rey':
    player.next()

# wait for events
main = GLib.MainLoop()
main.run()
```

## License

This work is available under the GNU General Public License (See COPYING).

Copyright © 2014, Tony Crisci
