#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"

if command -v x-terminal-emulator >/dev/null 2>&1; then
    x-terminal-emulator -e "$DIR/boc"
elif command -v gnome-terminal >/dev/null 2>&1; then
    gnome-terminal -- "$DIR/boc"
elif command -v konsole >/dev/null 2>&1; then
    konsole -e "$DIR/boc"
elif command -v xterm >/dev/null 2>&1; then
    xterm -e "$DIR/boc"
else
    echo "No terminal emulator found."
    echo "Running in current terminal..."
    "$DIR/boc"
fi
