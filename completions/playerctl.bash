#!/usr/bin/env bash

_playerctl_completions() {
	local cur="${COMP_WORDS[$COMP_CWORD]}"
	local prev="${COMP_WORDS[$COMP_CWORD - 1]}"
	local root_words="
		play
		pause
		play-pause
		stop
		next
		previous
		position
		volume
		status
		metadata
		open
		loop
		shuffle
		-h --help
		-p --player=
		-a --all-players
		-i --ignore-player=
		-f --format
		-F --follow
		-l --list-all
		-v --version"

	case $prev in 
		loop)
			COMPREPLY=($(compgen -W "none track playlist" -- "$cur"))
			return 0
			;;
		shuffle)
			COMPREPLY=($(compgen -W "on off" -- "$cur"))
			return 0
			;;
		-p|--player=|-i|--ignore-player=)
			COMPREPLY=($(compgen -W "$(playerctl --list-all)" -- "$cur"))
			return 0
			;;
		-f|--format)
			COMPREPLY=()
			return 0
			;;
		open)
			compopt -o default
			COMPREPLY=()
			;;
		position|volume|metadata)
			COMPREPLY=()
			return 0
			;;
		*)
			COMPREPLY=($(compgen -W "$root_words" -- "$cur"))
			return 0
			;;
	esac
}

complete -F _playerctl_completions playerctl
