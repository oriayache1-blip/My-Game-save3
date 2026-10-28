# FairFrame — free Geometry Dash frame counter

FairFrame is an offline-first Geode mod project for Geometry Dash 2.2081 on Windows. No accounts, license checks, daily caps, subscriptions, paid tiers, or network requests.

## Features
- Pause-menu controls to start/stop recording, export CSV, clear the session, and toggle the HUD.
- Records observed Jump press/release events and current PlayLayer update frame.
- Tracks attempt number, frame number, event gaps, and estimated hold length.
- Exports CSV files to the mod's Geode save directory.
- No input automation, physics changes, or paid features.

## Accuracy
This version is an input frame recorder and timing logger, not a replay-based success-window scanner. It does not automatically replay a level at shifted click frames to calculate the true survivable frame window. Frame counts are based on PlayLayer update ticks and should be tested with unusual physics/FPS-bypass setups.

## Build
A Windows GitHub Actions workflow builds the .geode artifact. Open the repository's Actions tab, choose the latest successful Build FairFrame run, then download its FairFrame-Windows-geode artifact ZIP. Extract the .geode from that ZIP and install through Geode.

## CSV output
File name: FairFrame-<level-name>-latest.csv

Columns: attempt,frame,action,delta_frames,hold_frames

## Fair-play promise
FairFrame only records and displays the player's own inputs. It never presses inputs, changes physics, alters a level, completes levels, or bypasses collision. All features are free.
