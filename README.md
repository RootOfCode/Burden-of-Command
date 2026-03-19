# Burden of Command — WWI Trench Management Tycoon 

A terminal-based strategy and management game set in the brutal conditions of World War I trench warfare. You take on the role of Captain Alistair Thorne, tasked with commanding a company through six weeks on the Western Front.

## Overview

Burden of Command is a turn-based simulation focused on leadership, morale, and survival rather than traditional combat mechanics. Every decision—whether tactical, logistical, or emotional—affects the lives of your men.

You will:

* Assign squads to tasks such as patrol, raid, repair, or rest
* Manage limited resources including food, ammunition, medical supplies, and tools
* Respond to unpredictable events like artillery barrages, gas attacks, and disease
* Make difficult moral decisions under pressure from high command
* Maintain morale and prevent collapse or mutiny

## Features

* Real-time terminal UI using ANSI escape sequences
* Squad-based management with distinct personalities and traits
* Dynamic weather system affecting gameplay
* Random and historical events that shape each playthrough
* Command actions that consume limited authority (Command Points)
* Trench engineering and upgrades system
* Persistent save system with multiple slots
* Narrative codex with historical context and character backstories
* Multiple difficulty levels including Ironman mode

## Gameplay Basics

### Objective

Survive 6 in-game weeks (84 turns) while keeping your company operational. Victory depends on maintaining manpower, morale, and supply levels.

### Core Systems

* **Morale**: Determines effectiveness and risk of collapse
* **Fatigue**: Impacts performance and recovery
* **Aggression**: Enemy pressure level; higher values increase danger
* **Resources**:

  * Food
  * Ammo
  * Medical Supplies
  * Tools

### Squad Tasks

Each squad can be assigned a task:

* Standby
* Patrol
* Raid
* Repair
* Rest

Each task has trade-offs between fatigue, morale, and resource consumption.

### Command Points

You have limited authority each turn to perform actions such as:

* Issuing rum rations
* Writing letters home
* Conducting medical triage
* Giving speeches
* Holding ceremonies

These actions can dramatically influence morale and survival.

### Upgrades

Use tools to improve trench conditions, such as:

* Duckboards (reduce mud fatigue)
* Sandbag reinforcements (reduce artillery damage)
* Dugouts (passive morale boost)
* Signal wire (extra command point)

## Controls

* Arrow Keys: Navigate
* Enter: Confirm selection
* Space: End turn
* O: Open orders
* C: Command actions
* I: Intel screen
* D: Dossier / Codex
* ESC: Menu / Back

## Installation

### Requirements

* POSIX-compatible system (Linux/macOS recommended)
* C compiler (e.g., `gcc` or `clang`)
* UTF-8 capable terminal
* Minimum terminal size: 80x24

### Build and Run

```bash
cc -O2 -o boc boc.c
./boc
```

## Save System

* 3 save slots available
* Versioned save format
* Ironman mode disables mid-run loading

## Difficulty Levels

* **Green Fields**: Easier resource management and fewer events
* **Into the Mud**: Balanced experience
* **No Man's Land**: Scarce resources and harsher events
* **God Help Us**: Ironman mode with maximum difficulty

## Winning and Losing

### Victory

* Survive all 84 turns
* Maintain a functioning company

### Defeat Conditions

* Loss of all men
* Morale collapse leading to mutiny

## Design Philosophy

This game emphasizes the psychological and logistical burden of command rather than glorifying combat. The goal is to simulate the tension between duty, survival, and humanity in extreme conditions.

## License

This project is provided as-is for educational and entertainment purposes.

## Notes

* Best experienced in a full terminal window
* Audio is not used; all feedback is visual/textual
* Each playthrough is different due to randomness and player choice
