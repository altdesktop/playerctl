# Playerctl

For true players only: spotify, vlc, audacious, bmp, xmms2, and others.

## About

Playerctl is a command-line utility and library for controlling media players that implement the [MPRIS](http://specifications.freedesktop.org/mpris-spec/latest/) D-Bus Interface Specification. Playerctl makes it easy to bind player actions, such as play and pause, to media keys.

For more advanced users, Playerctl provides an [introspectable](https://wiki.gnome.org/action/show/Projects/GObjectIntrospection) library available in your favorite scripting language that allows more detailed control like the ability to subscribe to media player events or get metadata such as artist and title for the playing track.

## Using the CLI

```
playerctl [--version] [--list-all] [--all-players] [--player=NAME] [--format=FORMAT] COMMAND
```

Pass the name of your player with the `--player` flag, or select all available players with the `--all-players` flag. You can find out what players are available to control with the `--list-all` switch. If no player is specified, it will use the first player it can find.

Here is a list of available commands:

```
play                    Command the player to play
pause                   Command the player to pause
play-pause              Command the player to toggle between play/pause
stop                    Command the player to stop
next                    Command the player to skip to the next track
previous                Command the player to skip to the previous track
position [OFFSET][+/-]  Command the player to go to the position or seek forward/backward OFFSET in seconds
volume [LEVEL][+/-]     Print or set the volume to LEVEL from 0.0 to 1.0
status                  Get the play status of the player
metadata [KEY...]       Print metadata information for the current track. If KEY is passed,
                        print only those values. KEY may be artist, title, album, or any key found in the metadata
open [URI]              Command for the player to open given URI.
                        URI can be either file path or remote URL.
```

## Printing Properties and Metadata

You can pass a format string with the `--format` argument to print properties in a specific format. Pass the variable you want to print in the format string between double braces like `{{ VARIABLE }}`. The variables available are either the name of the query command, or anything in the metadata map which can be viewed with `playerctl metadata`. You can use this to integrate playerctl into a statusline generator.

For a simple "now playing" banner:

```bash
playerctl metadata --format "Now playing: {{ artist }} - {{ album }} - {{ title }}"
# prints 'Now playing: Lana Del Rey - Born To Die - Video Games'
```

Included in the template language are some helper functions for common formatting that you can call on template variables.

```bash
playerctl metadata --format "Total length: {{ duration(mpris:length) }}"
# prints 'Total length: 3:23'
playerctl position --format "At position: {{ duration(position) }}"
# prints 'At position: 1:16'
playerctl metadata --format "Artist in lowercase: {{ lc(artist) }}"
# prints 'Artist in lowercase: lana del rey'
playerctl metadata status --format "STATUS: {{ uc(status) }}"
# prints 'STATUS: PLAYING'
```

| Function   | Argument | Description                             |
| ---------- | -------- | --------------------------------------- |
| `lc`       | string   | convert the string to lowercase         |
| `uc`       | string   | convert the string to uppercase         |
| `duration` | int      | convert the duration to hh:mm:ss format |

## Using the Library

To use a scripting library, find your favorite language from [this list](https://wiki.gnome.org/Projects/GObjectIntrospection/Users) and install the bindings library. Documentation for the library is hosted [here](https://dubstepdish.com/playerctl).

### Example Python Script

This example uses the [Python bindings](https://wiki.gnome.org/action/show/Projects/PyGObject).

```python
#!/usr/bin/env python3

from gi.repository import Playerctl, GLib

player = Playerctl.Player(player_name='vlc')

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
    # I meant some good music!
    player.next()

# wait for events
main = GLib.MainLoop()
main.run()
```

## Installing

First, check and see if the library is available from your package manager (if it is not, get someone to host a package for you) and also check the [releases](https://github.com/acrisci/playerctl/releases) page on github.

Using the cli and library requires [GLib](https://developer.gnome.org/glib/) (which is a dependency of almost all of these players as well, so you probably already have it). You can use the library in almost any programming language with the associated [introspection binding library](https://wiki.gnome.org/Projects/GObjectIntrospection/Users).

Additionally, you also need the following build dependencies:

[gobject-introspection](https://wiki.gnome.org/action/show/Projects/GObjectIntrospection) for building introspection data (configurable with the `introspection` meson option)

[gtk-doc](http://www.gtk.org/gtk-doc/) for building documentation (configurable with the `gtk-doc` meson option)

Fedora users also need to install `redhat-rpm-config`


To generate and build the project to contribute to development and install playerctl to `/`:

```
meson mesonbuild
sudo ninja -C mesonbuild install
```

Note that you need `meson >= 0.46.0` installed. In case your distro only has an older version of meson in its repository you can install the newest version via pip:

```
pip3 install meson
```

Also keep in mind that gtk-doc and gobject-introspection are enabled by default, you can disable them with `-Dintrospection=false` and `-Dgtk-doc=false`.

If you don't want to install playerctl to `/` you can install it elsewhere by exporting `DESTDIR` before invoking ninja, e.g.:

```
export PREFIX="/usr/local"
meson --prefix="${PREFIX}" --libdir="${PREFIX}/lib" mesonbuild
export DESTDIR="$(pwd)/install"
ninja -C mesonbuild install
```

You can use it later on by exporting the following variables:

```
export LD_LIBRARY_PATH="$DESTDIR/${PREFIX}/lib/:$LD_LIBRARY_PATH"
export GI_TYPELIB_PATH="$DESTDIR/${PREFIX}/lib/:$GI_TYPELIB_PATH"
export PATH="$DESTDIR/${PREFIX}/bin:$PATH"
```

## License

This work is available under the GNU Lesser General Public License (See COPYING).

Copyright © 2014, Tony Crisci
