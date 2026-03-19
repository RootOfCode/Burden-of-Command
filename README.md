# BURDEN OF COMMAND
### *A WWI Trench Management Tycoon*

```
╔════════════════════════════════════════════════╗
║  B U R D E N   O F   C O M M A N D            ║
║     A  W W I  T r e n c h  T y c o o n        ║
╚════════════════════════════════════════════════╝
  11th East Lancashire Regt.  │  Passchendaele  │  1917
```

> *"The men fight less for king and country than for the man beside them."*

---

## Overview

**Burden of Command** is a single-file terminal strategy game written in C. You play Captain Alistair Thorne of the 11th East Lancashire Regiment on the Western Front in 1917. Your objective is simple: keep your company alive and functional for six weeks while Brigade Headquarters demands the impossible from twelve miles behind the line.

The game runs entirely in a standard 80×24 ANSI terminal. No graphics, no sound, no dependencies beyond a C compiler.

---

## Building

### Using Make (recommended)

```bash
make          # build release binary
make run      # build and launch immediately
make debug    # build with sanitisers and run
make clean    # remove compiled files and archives
make install  # install to /usr/local/bin  (POSIX only)
make dist     # create source archive (.tar.gz / .zip)
make help     # list all targets and variables
```

Override the compiler or install prefix at any time:

```bash
make CC=clang
make install PREFIX=~/.local
```

### Without Make — Linux / macOS

```bash
cc -O2 -o boc boc.c && ./boc
```

### Without Make — Windows (MinGW / MSYS2 / Git Bash)

```bash
gcc -O2 -o boc.exe boc.c && ./boc.exe
```

### Without Make — Windows (MSVC Developer Command Prompt)

```bat
cl /O2 /Fe:boc.exe boc.c
boc.exe
```

### Requirements

| Platform | Compiler | Notes |
|----------|----------|-------|
| Linux | `cc`, `gcc`, `clang` | Any C99-capable compiler |
| macOS | `cc`, `clang` | Ships with Xcode Command Line Tools |
| Windows 10+ | `gcc` (MinGW/MSYS2) or MSVC 2015+ | Requires VT100-capable terminal |

**All platforms:** terminal minimum **80 columns × 24 rows**, UTF-8 character encoding.

**Windows-specific notes:**
- Requires **Windows 10 version 1511** or later for VT100/ANSI escape support (`ENABLE_VIRTUAL_TERMINAL_PROCESSING`). Earlier versions will display garbled escape sequences.
- Use **Windows Terminal**, **PowerShell**, or a modern `cmd.exe`. Legacy `cmd.exe` on older Windows does not support ANSI colours.
- The game sets the console to UTF-8 (`CP 65001`) automatically on startup. If box-drawing characters still look wrong, set your terminal font to a Unicode-capable typeface: **Cascadia Mono**, **Consolas**, or **DejaVu Sans Mono** all work well.
- Save files are written to the current working directory.

**Linux / macOS notes:**
- Check your terminal size: `echo $COLUMNS x $LINES`
- Check your locale: `locale | grep LANG`

---

## The Premise

It is the summer of 1917. The Third Battle of Ypres — Passchendaele — is underway. Your four-squad company holds a sector of the front-line trench near Flanders, Belgium. You have received orders to hold the line for six weeks.

You have food, ammunition, medical supplies, and tools. You have four squads of exhausted men, each led by a sergeant with his own personality and history. You have three command points a day to spend on personal leadership. You have a deteriorating relationship with a Brigade HQ that has not visited the front in three months.

Survive. That is all that is asked. It is not a small ask.

---

## Controls

### Main Game Screen

| Key | Action |
|-----|--------|
| `↑ ↓` or `← →` | Select squad |
| `SPACE` | End turn (advance time by half a day) |
| `O` | Open orders menu for selected squad |
| `C` | Command Actions screen |
| `R` | Resource Management screen |
| `I` | Intelligence Report screen |
| `D` | Squad Dossier for selected squad |
| `ESC` | Pause menu |
| `Q` | Quit to main menu |

### Orders Mode (`O`)

| Key | Action |
|-----|--------|
| `← →` | Cycle through available orders |
| `ENTER` | Confirm and assign order |
| `ESC` | Cancel |

---

## Squads

You command four sections:

| Squad | Sergeant | Personality | Starting State |
|-------|----------|-------------|----------------|
| **Alpha** | Sgt. Thomas Harris | Brave | 7/8 men, good morale, low fatigue |
| **Bravo** | Sgt. William Moore | Steadfast | 8/8 men, good morale, fresh |
| **Charlie** | Sgt. Owen Lewis | Drunkard | 5/8 men, poor morale, exhausted |
| **Delta** | Sgt. Arthur Bell | Cowardly | 6/8 men, fair morale, tired |

Each sergeant's **personality** modifies the effectiveness of all fatigue and morale changes:

| Personality | Multiplier | Effect |
|-------------|-----------|--------|
| Steadfast | ×1.00 | Consistent; no bonus or penalty |
| Brave | ×1.20 | +20% to all changes; pushes hard but burns fast |
| Cowardly | ×0.70 | −30% effectiveness; poor under sustained pressure |
| Drunkard | ×0.85 | −15% effectiveness; unpredictable in crisis |

---

## Orders

Assign one order to each squad each turn. Orders persist until changed.

| Order | Fatigue | Morale | Ammo | Other |
|-------|---------|--------|------|-------|
| **STANDBY** | −5 | 0 | Low | Safe default. Conserves fatigue. |
| **PATROL** | +10 | +3 | Med | Scout no-man's-land. Best morale return. |
| **RAID** | +20 | +4 | High | Strike enemy lines. High risk, high reward. |
| **REPAIR** | +5 | 0 | Min | Fix trench works. Generates tools each turn. |
| **REST** | −15 | +5 | Min | Stand down. Fastest recovery. |
| **FORAGE** | +12 | −2 | None | Scavenge farms. Finds food; costs morale. |
| **SCAVENGE** | +15 | −3 | None | Strip no-man's-land. Finds ammo and tools. |

**Key interactions:**
- Squads above 80% fatigue lose 4 morale/turn regardless of order
- PATROL bonus is multiplied by the current **Ammo Policy**
- FORAGE and SCAVENGE have low raid resistance — watch sector threats
- Squads at critical morale (< 10%) may desert one man per turn

---

## Resources

Four resources balanced across 84 turns (6 weeks × 7 days × 2 half-days):

| Resource | Cap | Consumed by | Critical threshold |
|----------|-----|-------------|-------------------|
| **Food** | 100 (125 w/ Food Cache) | Men alive × ration policy | < 15: morale drain all squads |
| **Ammo** | 100 (125 w/ Munitions Store) | Task × ammo policy | < 10: patrol morale penalty |
| **Meds** | 50 | Wounds + gas attacks | 0: wounds become fatal |
| **Tools** | 50 | Upgrades (spent); REPAIR squads (gained) | — |

### Ration Policy

Set in **Resources → Policies**. Applies a food multiplier and morale effect every turn.

| Policy | Food × | Morale/turn | Notes |
|--------|--------|-------------|-------|
| FULL | ×1.00 | 0 | Standard. Men properly fed. |
| HALF | ×0.75 | −1 | Manageable short-term. |
| QUARTER | ×0.55 | −4 | Significant morale damage within a week. |
| EMERGENCY | ×0.30 | −8 | Last resort. Men will not endure this long. |

### Ammo Policy

| Policy | Ammo × | Patrol bonus × | Raid resist | Notes |
|--------|--------|---------------|-------------|-------|
| CONSERVE | ×0.60 | ×0.60 | −10% | Stretches supply. Weakens all engagements. |
| NORMAL | ×1.00 | ×1.00 | 0% | Balanced. |
| LIBERAL | ×1.50 | ×1.40 | +15% | Maximum effectiveness. Budget carefully. |

### Barter

**Resources → Barter** lets you trade resources at unfavourable rates (e.g. 20 food → 8 ammo). Twelve trade pairs cover all four resource combinations. Barter is a last resort — rates reflect the chaos of frontline supply chains.

---

## Command Points

The header shows current CP (●●●). Maximum 3/day, or 4 with **Signal Wire**. One CP regenerates each AM turn.

Spend via **Command Actions** (`C`):

| Action | CP | Food | Meds | Effect |
|--------|----|------|------|--------|
| Rum Ration | 1 | −5 | — | +15 morale, selected squad |
| Write Letters | 1 | — | — | +10 morale, selected squad |
| Medical Triage | 1 | — | −5 | −25 fatigue, selected squad |
| Inspect & Reprimand | 0 | — | — | −8 fatigue, −5 morale (free) |
| Officer's Speech | 2 | — | — | +8 morale, all squads |
| Emergency Rations | 2 | −20 | — | −15 fatigue, all squads |
| Medal Ceremony | 3 | — | — | +20 morale, selected squad; +1 medal |
| Compassionate Leave | 2 | — | — | −1 man, +15 morale |
| Treat Wounded | 1 | — | −3 | Heal 2 wounds in selected squad |
| Supply Request | 2 | — | — | Petition HQ for a convoy (quality scales with reputation) |

The **Rum Store** upgrade makes Rum Ration cost 0 CP (food cost remains).

---

## Wounds

Artillery, raids, gas, snipers, and friendly fire produce **walking wounded** in addition to kills. Displayed as `+2W` per squad in the main view.

- Wounds passively consume **1 med per wound per turn**
- Untreated wounds (meds = 0) have a 15% chance/turn of killing the man
- Use **Treat Wounded** command action or build the **Field Hospital** upgrade

---

## Trench Upgrades

Accessed via **Pause → Trench Upgrades**. Spend tools to permanently improve the sector.

| Upgrade | Cost | Passive Effect |
|---------|------|----------------|
| Duckboards | 8 | Rain/storm fatigue halved |
| Sandbag Revetments | 12 | Artillery casualties −1 |
| Officers' Dugout | 15 | +1 morale/turn all squads |
| Fire-Step Periscope | 10 | PATROL +2 extra morale/turn |
| Lewis Gun Nest | 20 | +15% raid resistance all squads |
| Trench Sump | 6 | Disease/influenza chance halved |
| Signal Wire | 8 | +1 CP per AM turn |
| Rum Store | 10 | Rum Ration costs 0 CP |
| Field Hospital | 18 | Auto-heals 1 wound/turn; halves triage med cost |
| Observation Post | 14 | Sniper/raid events −25%; sector threat visibility |
| Munitions Store | 12 | Ammo cap +25; ammo damp events eliminated |
| Food Cache | 10 | Food cap +25; rat and spoilage events eliminated |

---

## Notable Soldiers

Each squad has two **named privates** with a special trait. They contribute passive effects every turn and can be killed by hostile events.

| Trait | Notable | Passive Effect |
|-------|---------|----------------|
| Sharpshooter | Pte. Whitmore / Pte. Finch | +10% raid resist; may intercept snipers |
| Cook | Pte. Beecroft / Pte. Colby | Saves 1 food per turn |
| Medic | Pte. Morley | Auto-treats 1 wound/turn (costs 1 med) |
| Scrounger | Pte. Pollard | Finds 1 ammo or 1 tool per turn at random |
| Musician | Pte. Darton | +2 morale per turn |
| Runner | Pte. Stubbs | Convoy ETA −1 turn |

Deaths are recorded in the **Field Diary** by full name.

---

## Sector Threats

Four sectors A–D each with a 0–100 threat level shown in the map panel and Intel screen.

- Rises with aggression and **Sector Assault** random events
- Falls when the corresponding squad is on **PATROL** or **RAID**
- High threat increases likelihood of raids and artillery in that sector
- **Observation Post** improves awareness

---

## HQ Reputation

Shown as `Rep:N` in the header (0–100). Affects convoy quality and Supply Request outcomes. Modified by how you respond to **HQ Dispatches**.

---

## HQ Dispatches

Five scripted orders interrupt the game with a full-screen choice modal.

| Turn | Order | Comply | Defy |
|------|-------|--------|------|
| 5 | Night Raid | Forced raid; −ammo; −aggression | +aggression; −morale; −rep |
| 16 | Ration Reduction | −food; −morale; +rep | Next convoy delayed; −rep |
| 28 | Pioneer Secondment | −2 men; +ammo; +rep | +aggression; −morale; −rep |
| 44 | Defensive Posture | All STANDBY; +ammo; +rep | Offensive hits disorganised sector |
| 60 | VC Nomination | +morale; +medals; +rep | −morale; −rep |

---

## Random Events (18 total)

Artillery · Enemy Raid · Gas Attack · Mail from Home · Rats · Influenza · Sergeant Breakdown · Supply Convoy · Reinforcements · Sniper · Fraternisation · Hero Moment · Friendly Fire · Enemy Cache · Food Spoilage · Ammo Damp · Wound Heals · Sector Assault

All events scale with difficulty, aggression, and weather.

---

## Weather

| Weather | Fat/turn | Food/turn | Agg drift | Raid × |
|---------|----------|-----------|-----------|--------|
| Clear | 0 | 0 | 0 | ×1.00 |
| Rainy | +3 | +1 | 0 | ×1.00 |
| Foggy | +1 | 0 | 0 | ×1.50 |
| Snowing | +4 | +2 | +2 | ×0.75 |
| Storm | +6 | +1 | +4 | ×1.20 |

**Duckboards** halve fatigue in rain and storm.

---

## Difficulty

| Difficulty | Food × | Events × | Morale × | Score × | Saves |
|------------|--------|----------|----------|---------|-------|
| Green Fields | ×0.6 | ×0.6 | ×0.7 | ×0.8 | Yes |
| Into the Mud | ×1.0 | ×1.0 | ×1.0 | ×1.0 | Yes |
| No Man's Land | ×1.4 | ×1.5 | ×1.3 | ×1.4 | Yes |
| God Help Us | ×1.6 | ×1.8 | ×1.5 | ×2.0 | **No** |

*God Help Us* is ironman mode — no loading mid-campaign.

---

## End States

| State | Condition |
|-------|-----------|
| **Victory** | Survive all 84 turns — your unit is relieved |
| **Defeat** | All men dead |
| **Mutiny** | All squads simultaneously below 5 morale |

---

## Scoring

```
Score = (Turns × 5) + (Men × 20) + (Morale × 3) + (Medals × 50)
      + (Rep/10 × 30) + (Upgrades × 25)
      + 500 (victory)  −  200 (mutiny)
      × Difficulty multiplier
```

| Grade | Score |
|-------|-------|
| S+ | 1400+ |
| S | 1000+ |
| A | 700+ |
| B | 500+ |
| C | 300+ |
| D | 120+ |
| F | < 120 |

---

## Save System

Three save slots stored as binary files in the working directory:

```
boc_s0.bin   boc_s1.bin   boc_s2.bin
```

Each slot shows date, week, men alive, and difficulty at save time. Saves are versioned — files from earlier versions are not compatible.

Access via **Main menu → Load Game** or **ESC → Pause → Save / Load**.

*Ironman (God Help Us) mode disables loading.*

---

## Screens

| Screen | Key | Description |
|--------|-----|-------------|
| **Main Command** | — | Squads, resources, sector map, message log |
| **Orders** | `O` | Assign task to selected squad |
| **Command Actions** | `C` | Spend CP on morale/fatigue/resource actions |
| **Resource Management** | `R` | Stock overview, sparklines, barter, policies |
| **Intelligence Report** | `I` | Event queue, HQ dispatches, sector status, resource forecast |
| **Squad Dossier** | `D` | Full stats, wounds, notables, combat record, personnel file |
| **Trench Upgrades** | Pause → Upgrades | Build permanent sector improvements |
| **Codex** | Pause → Codex | 15 lore entries on the war, the men, the weapons |
| **Field Diary** | Pause → Diary | Scrollable timestamped log of all campaign events |
| **HQ Dispatch** | Automatic | Binary compliance choices at scripted turns |

---

## Files

```
boc.c            — entire game source (~2,400 lines, C99)
Makefile         — cross-platform build system (Linux / macOS / Windows MinGW)
README.md        — this file
description.md   — itch.io store page copy
boc_s0.bin       — save slot 0  (created on first save)
boc_s1.bin       — save slot 1
boc_s2.bin       — save slot 2
```

---

## Design Notes

The game is written in a deliberately **data-driven** style. All behaviour is defined in flat tables near the top of the source. To change how a task works, edit `TASK_DEFS`. To add a difficulty level, add a row to `DIFF_DEFS`. To add a random event, extend `RAND_PROBS` and add a case to `random_events()`. No behaviour is hardcoded into logic that belongs in data.

The single-file structure is intentional. The entire game compiles with one command on Linux, macOS, and Windows. The Makefile provides convenience targets (`run`, `debug`, `install`, `dist`) but is not required to build the game.

---

## Historical Note

The 11th East Lancashire Regiment, Captain Alistair Thorne, and the individual soldiers depicted are fictional. The conditions, equipment, events, and institutional failures they experience are not.

The Third Battle of Ypres (Passchendaele) ran from July to November 1917 and resulted in approximately 325,000 Allied casualties for territorial gains measured in a few miles of Belgian mud. The average life expectancy of a British second lieutenant on the Western Front in 1917 was approximately six weeks.

The men who served in the trenches were ordinary people placed in extraordinary circumstances. They endured. Many did not return. None should be forgotten.

---

## License

MIT License

Copyright (c) 2025

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
