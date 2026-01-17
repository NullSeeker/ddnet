# TAS (Tool-Assisted Speedrun) Features

## Overview
This implementation provides advanced TAS functionality similar to KRX Client, allowing players to create and manipulate recordings in an isolated environment.

## Fixed Issues
- **UI Bug Fix**: Fixed the issue where users couldn't type the filename in the save field. The TAS filename input field now properly updates the configuration and the selected file.

## New Features

### 1. Isolated TAS World
- When entering TAS mode, a separate game world is created with a copy of your character
- The main character remains stationary during TAS recording/playback
- Complete isolation from the main game world
- No interaction with other players or the main game world during TAS sessions

### 2. Advanced Playback Controls
- **Speed Control**: Multiple speed options for playback
  - Slow motion (`<`): Half normal speed
  - Normal speed (`||`): Standard speed
  - Fast forward (`>`): Double speed
  - Fast forward (`>>`): Quadruple speed

- **Rewind Controls**: Ability to rewind through the recording
  - Slow rewind (`<`)
  - Medium rewind (`<<`)
  - Fast rewind (`<<<`)

### 3. Console Commands
- `tas_speed_up`: Increase playback speed
- `tas_slow_down`: Decrease playback speed  
- `tas_normal_speed`: Set normal playback speed
- `tas_rewind_back`: Rewind backwards slowly
- `tas_fast_forward`: Fast forward through recording

### 4. Time Manipulation
- Real-time adjustment of playback speed
- Frame-perfect control during recording and playback
- Ability to pause, slow down, speed up, or rewind recordings

## Usage Instructions

1. Go to Settings → TAS tab
2. Enter TAS mode using the "Enter TAS" button
3. Start recording with the "Record" button
4. Use the playback controls to adjust speed and navigate through recordings
5. Save/load recordings using the filename field and folder icon

## Technical Implementation

The TAS system works by:
1. Creating a separate game world instance for TAS operations
2. Maintaining a history of character states and inputs
3. Providing frame-perfect control over the simulation
4. Isolating the TAS world from the main game to prevent interference

The system ensures that the main player character does not move while TAS recording is active, providing a safe environment for creating precise recordings.