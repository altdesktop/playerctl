#compdef playerctl

typeset -A opt_args
__playerctl() {
	command playerctl "$@" 2>/dev/null
}

__playerctl_ctx() {
	local -a player_opts=(
		${(kv)opt_args[(I)-p|--player]}
		${(kv)opt_args[(I)-i|--ignore-player]}
		${(kv)opt_args[(I)-a|--all-players]}
	)
	__playerctl "$player_opts[@]" "$@"
}

local -a playercmd_loop=(/$'(none|track|playlist)\0'/ ':(none track playlist)')
local -a playercmd_shuffle=(/$'(on|off)\0'/ ':(on off)')

(( $+functions[_playerctl_players] )) ||
_playerctl_players() {
	local -a players=( ${(@f)"$(__playerctl --list-all)"} )
	players+=( "%all" )
	compadd "$@" -a players
}

(( $+functions[_playerctl_metadata_keys] )) ||
_playerctl_metadata_keys() {
	local -a keys
	__playerctl_ctx metadata |
	while read PLAYER KEY VALUE; do
		keys+="$KEY"
	done
	_multi_parts "$@" -i ":" keys
}
local -a playerctl_command_metadata_keys=(/$'[^\0]#\0'/ ':keys:key:_playerctl_metadata_keys')

local -a playerctl_command
_regex_words commands 'playerctl command' \
	'play:Command the player to play' \
	'pause:Command the player to pause' \
	'play-pause:Command the player to toggle between play/pause' \
	'stop:Command the player to stop' \
	'next:Command the player to skip to the next track' \
	'previous:Command the player to skip to the previous track' \
	'position:Command the player to go or seek to the position' \
	'volume:Print or set the volume level from 0.0 to 1.0' \
	'status:Get the play status of the player' \
	'metadata:Print the metadata information for the current track:$playerctl_command_metadata_keys' \
	'open:Command the player to open the given URI' \
	'loop:Print or set the loop status:$playercmd_loop' \
	'shuffle:Print or set the shuffle status:$playercmd_shuffle'
playerctl_command=( /$'[^\0]#\0'/ "$reply[@]" )
_regex_arguments _playerctl_command "$playerctl_command[@]"

_arguments -S -s\
	'(-h --help)'{-h,--help}'[Show help message and quit]' \
	'(-v --version)'{-v,--version}'[Print version information and quit]' \
	'(-l --list-all)'{-l,--list-all}'[List all available players]' \
	'(-F, --follow)'{-F,--follow}'[Bock and append the query to output when it changes]' \
	'(-f --format)'{-f,--format=}'[Format string for printing properties and metadata]' \
	'(-i --ignore-player)'{-i,--ignore-player=}'[Comma separated list of players to ignore]:players:_sequence _playerctl_players' \
	'(-a --all-players)'{-a,--all-players}'[Control all players instead of just the first]' \
	'(-p --player)'{-p,--player=}'[Comma separated list of players to control]:players:_sequence _playerctl_players' \
	'*::playerctl command:= _playerctl_command'
