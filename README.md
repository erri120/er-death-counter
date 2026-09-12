# Elden Ring Death Counter

An Elden Ring mod that displays an on-screen death counter overlay.

## Features

- Total death counter (read from the save game data)
- Session counter: deaths since starting the game
- Boss counter: consecutive deaths at the current boss, shown while the fight
  is active and until the boss is defeated

## Configuration

The mod reads `death_counter.ini` from its own directory (next to the DLL).

## Logging

The mod writes a log file next to the DLL: `death_counter.log`.

## License

The embedded font (EB Garamond) is licensed under the SIL Open Font License
1.1, see `fonts/OFL.txt`.
