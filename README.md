# BURDEN OF COMMAND
A WWI Trench Management Tycoon (Terminal Game)

## Overview
Burden of Command is a terminal-based management and survival simulation set on the Western Front in 1917 during World War I.

You assume the role of Captain Alistair Thorne, responsible for maintaining a small company of soldiers stationed in trench lines. Your objective is simple but brutal: keep your unit alive and functional long enough to be relieved.

The game runs entirely in a terminal using ANSI escape codes and is written in Common Lisp. It is designed to be played in a standard 80x24 terminal.

Survive six weeks of artillery bombardment, raids, disease, shortages, exhaustion, and collapsing morale.

## Features

- Fully terminal-based interface (TUI)
- Dynamic squad management
- Resource management (food, ammunition, medical supplies, tools)
- Random battlefield events
- Scheduled supply and reinforcement convoys
- Weather simulation
- Morale and fatigue systems
- Sergeant personality effects
- Save and load functionality

## Requirements

- SBCL (Steel Bank Common Lisp)
- A Unix-like environment
- A terminal that supports ANSI escape sequences

The game relies on the `stty` command to switch the terminal into raw mode.

## Running the Game

Clone or download the source file and run:

sbcl --load burden-of-command.lisp

The game will start immediately after loading.

## Terminal Requirements

Recommended terminal size:

80 columns x 24 rows

If the terminal is smaller than this, the interface may render incorrectly.

## Controls

General Controls:

SPACE        End turn
O            Enter Orders Mode for selected squad
Arrow Keys   Select squad
S            Save game
L            Load game
Q            Quit

Orders Mode:

LEFT/RIGHT   Cycle available orders
ENTER        Confirm order
ESC          Cancel

## Gameplay

Each turn represents half a day.

You must manage:

- Squad morale
- Fatigue levels
- Food supply
- Ammunition reserves
- Medical supplies

Squads can be ordered to perform different tasks:

Standby
Patrol
Raid
Repair
Rest

Each task affects fatigue, morale, and resource consumption differently.

## Random Events

The battlefield is unpredictable. Possible events include:

- Artillery bombardments
- Enemy trench raids
- Gas attacks
- Disease outbreaks
- Rats eating rations
- Mail from home boosting morale
- Sergeant psychological breakdowns

Supply convoys and reinforcements may arrive after delays.

## Winning the Game

Survive for six weeks until your company is relieved from the front line.

## Losing the Game

You lose if:

- All soldiers die
- Morale collapses across all squads, causing mutiny

## Saving and Loading

The game can be saved at any time.

The save file is written to:

boc.sav

Use the following keys:

S   Save the game
L   Load the game

## Development Notes

This project demonstrates:

- ANSI terminal UI rendering
- Turn-based simulation
- Event queue scheduling
- Terminal raw input handling in SBCL
- Pure Common Lisp game architecture

All rendering is done manually using ANSI cursor positioning.

## License

This project is released into the public domain.

You are free to modify, distribute, and build upon it.

## Acknowledgments

Inspired by historical accounts of trench warfare during World War I.

