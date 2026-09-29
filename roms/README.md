# ROMs

Put Game Boy games (`.gb` files) in this folder, then run:

```
make run ROM="roms/Your Game.gb"
```

or start `./build/boy_dmg` with no file and pick one from the window that opens.

Only `libbet.gb` is stored in the repository. Any other game you add here stays on
your computer, because `.gitignore` keeps it out of Git. Most games may not be
shared publicly, so please keep it that way.

## Included game

**Libbet and the Magic Floor** (`libbet.gb`)
by Damian Yerrick, based on *Magic Floor* by Martin Korth.
Released under the zlib License, which allows it to be redistributed. See
`LICENSE-libbet.txt`. Source code: https://github.com/pinobatch/libbet

Controls: arrow keys roll, hold Z (A button) plus an arrow to jump, Return starts,
Shift or Backspace (Select) shows a demo.

## Other free games to try

- **Tobu Tobu Girl**, an arcade platformer (free on itch.io, open source)
- Homebrew made for the original Game Boy generally works best. Game Boy Color only
  games won't run, since this emulator is an original Game Boy (DMG).
