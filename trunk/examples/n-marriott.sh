#!/bin/sh -x

[ ! -z "$TMUX" ] && exit

# I alias this script to "session" in .profile and use it to reconnect to
# the main session (0) on my main tmux server.

SOCKET=/tmp/tmux-1000-main

TMUX="tmux -S $SOCKET"

$TMUX has -s0 2>/dev/null || $TMUX start
$TMUX attach -d -s0
