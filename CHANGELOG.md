# Changelog

## Version 2.2.1

Version 2.2.1 contains some bugfixes and new features.

* Fix a crash when the `emoji()` template function is used (#167)
* Add a `shift` command to `playerctld` to shift the active player (#173)
* Fix a crash when system players are present (#175)

## Version 2.1.1

Version 2.1.1 contains some bugfixes and new features.

Playerctl now has a test suite that covers most features of the CLI.

**playerctld**

* Add `playerctld`: an activatable DBus service for selecting the most recently active player (#161, #164, #128)

**CLI**

* Add the `markup_escape()` formatter function (#133)
* Mark the `emoji()` formatter function as no longer experimental
* Add the `default()` formatter function (#142, fd0b4ab)
* Add the special `%any` player token for prioritizing player selection (#143)
* Add bash completions (#153)
* Add debug logging (#152)
* Rewrite and expand manpage with `mdoc(7)` (#130)
* Attempt to autostart `playerctld` if it is present in players
* bugfix: incorrect error message for shuffle command (#158)
* bugfix: don't crash if no system bus is present (2330b64f)
* bugfix: don't crash if given a nonexistent format function (#162)

**Build**

* Required meson version is now `0.50.0`.

## Version 2.0.2

Version 2.0.2 contains some minor bugfixes for the CLI and build system.

The author would like to inform you as part of my effort to improve media player integration on the Linux Desktop, I have fixed many bugs in Electron based media players that should be available soon (see #40, #81, #35 which were closed recently).

**CLI**

* Regression: exit 1 when no players are found (#126, #119)
* Regression: fix sort order for `--player` command (#112)
* Handle nonfile uris in the `open` command (#122)

**Build**

* Fix documentation of the `--follow` flag (#117)
* Update manpage release date at build time (#118)
* fix gir build on cross compilation (#120)

## Version 2.0.1

Version 2.0.1 includes new major features and breaking changes to the library and CLI.

**CLI**

* Add `--ignore-player` flag to ignore specific players (#2)
* Add `--follow` flag to block and print updated values when they change (#37, #98, #101)
* The `--player` command acts on the first player without `--all-players` (breaking) (#54)
* Accept multiple keys for `metadata [key]` command (#68)
* `metadata` command has tabular output. (breaking) (#72)
* Add `--format [fmt]` for metadata formatting (#73)
* Add `duration()` template formatter for formatting durations (#75)
* Print player name and instance with format strings (#90)
* Add command to get and set `shuffle` status (#92)
* Add a command to get and set `loop` status (#99)
* Add the `open` command to open a URI with the player (#79)
* Fix some errors with utf8 printing (#80)
* Skip players from selection when they don't support a command (determined by the `can-*` properties)
* Select all player instances with the `--player` and `--ignore-player` command
* Print help information to stdout (not stderr) when no arguments are passed

**Library**

* add `playerctl_list_players()` to public api for listing players (#47)
* Implement the "seeked" signal on the player (#94)
* Add the "volume" signal on the player (#95)
* Deprecate the "play", "pause", and "stopped" signal for a single "status" signal (#96)
* Add the `PlayerctlPlayerManager()` class (#100)
* Cache and compute the position property (#102)
* Remove chaining abilities from the library (breaking)
* Library query functions return `NULL` instead of empty string when properties aren't found (breaking)
* Deprecate `status` property in favor of the `playback-status` property as an enum
* Add library functions for `shuffle` and `loop` status (#92, #99)
* Deprecate setting volume via the object properties interface
* Fix the "exit" signal
* Add properties "can-control", "can-play", "can-pause", "can-seek", "can-go-next", "can-go-previous"
* Add the "source" property to determine the source of the player (session or system bus)
* Change first keyword arg for `playerctl_player_new()` from `name` to `player_name` (breaking)
* Add `playerctl_player_new_for_source()` to select players based on the source (session or system bus)
* Add `playerctl_player_new_from_name()` to create a player from a PlayerManager name
* `playerctl_player_new()` selects an instance of the `player_name` if found
* Add documentation for the entire public library API

**Build**

* Remove autotools and switch to the meson build system (breaking) (#57)
* Fix various compiler warnings (#97)
* Remove library version from pkg-config name and add it to the so in the standard way (new pkg-config name is just `playerctl`).

## Version 0.6.1

Version 0.6.1 includes bug fixes and some minor features.

* Bugfix: unref of a null player when no players are present
* Playerctl now searches the system bus for players
* Parse trackid as a string as a workaround for noncompliant players
* Various meson fixes

## Version 0.6.0

Version 0.6.0 includes bug fixes and new features.

* control multiple players at once by putting commas between the names
* add the --all-players option to control all players at once
* lib: better cache invalidation strategy for getting properties
* bugfix: Set position in fractional seconds
* Fix various memory leaks and errors

NOTE: This will be the last minor release that uses autotools. Playerctl will switch to the meson build system as of the next minor release.

Github releases will have a debian package and an rpm, but these will soon be deprecated as package maintainers create official packages for distros.

## Version 0.5.0

Version 0.5.0 includes some new features.

New features:

- Add workaround for Spotify to get metadata
- Add `position` cli command to query and set position
- Add `position` property to Player and method to set position to
  library

## Version 0.4.2

Version 0.4.2 includes several important bug fixes.

- Send `Play` directly instead of a `PlayPause` message depending on player status. This was an exception for Spotify that is no longer needed.
- Fix memory errors when an initialization error occurs.

## Version 0.4.1

This version includes a fix to support unicode characters when printing metadata.

## Version 0.4.0

This version adds the following features and bugfixes

- List players with cli `-l` option.
- Fix a bug in the build for some platforms
- Remove claim of mplayer support

## Version 0.3.0

This release includes some major bugfixes and some new features mostly for the library for use in applications.

- Add the "stop" library and cli command
- Add the "exit" signal - emitted when the player exits
- Implement player class memory management
- Add version macros

The following quirks have been corrected (should not be breaking)

- Player "player_name" property getter returns the player name and not the DBus name
- Player "stop" event correctly emits "stop" and not "pause"
- Add include guards so only `<playerctl/playerctl.h>` can be included directly

Additional packages available by request

## Version 0.2.1

This minor release adds a pkg-config file and relicenses the code under the LGPL.

## Version 0.2.0

This release adds convenient metadata accessors and improves error handling

- Add get_artist method to player
- Add get_title method to player
- Add get_album method to player
- Add get_metadata_prop to player
- Add [KEY] option to metadata cli
- Bugfix: gracefully handle property access when connection to dbus fails by returning empty properties

## Version 0.1.0

This release adds some new player commands and improves error handling

- Add the "next" CLI command and player method used to switch to the next track
- Add the "previous" CLI command and player method used to switch to the previous track
- Print an error message when no players are found in the CLI and propagate an error on initialization in this case in the library
- Print an error message when a command fails in the CLI and propagate an error in this case in the library

# Version 0.0.1

Playerctl is a command-line utility and library for controlling media players that implement the [MPRIS](http://specifications.freedesktop.org/mpris-spec/latest/) D-Bus Interface Specification. Playerctl makes it easy to bind player actions, such as play and pause, to media keys.

For more advanced users, Playerctl provides an [introspectable](https://wiki.gnome.org/action/show/Projects/GObjectIntrospection) library available in your favorite scripting language that allows more detailed control like the ability to subscribe to media player events or get metadata such as artist and title for the playing track.
