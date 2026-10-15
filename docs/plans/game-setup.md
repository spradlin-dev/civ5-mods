# Plan: game setup by the AI

Not started. Saved on 2026-10-15. It may not be worth building: see "Is it worth it".

## Why

Today the human sets up every game by hand, and the AI joins only after the game
has started. With this, the AI could pick the settings and start the game itself,
or set up a game the two players agreed on.

## What already works for it

When the mod is turned on in the Mods menu, the game reloads with our DLL before the
setup screen opens. All game settings (civilizations, teams, map, difficulty, speed,
victory types) are kept inside that DLL, so the mod can read and change them.

## What to build

- Start the MCP server as soon as the DLL loads, not when a game begins.
- A setup screen of our own that runs the AI's requests while no game exists yet.
  During play the game does this; before a game starts, nothing does.
- Tools to list the choices a player sees on the setup screen, change them, and
  start the game; maybe also one to load a save.

## Limits

- The human still has to open the Mods menu, turn the mod on and click Next. At the
  main menu the game runs its own DLL, which has no network access.
- The AI sees only what a human on the setup screen sees.

## Is it worth it

Our setup screen would replace the game's own, and so would Really Advanced Setup.
The two mods could not be used together, and players who use Really Advanced Setup
would lose its options. Unless the requests can run from some other screen, this
costs more than it gives.

## A possible way around it

Really Advanced Setup replaces only two of the game's screens: advanced setup and the
loading screen. Our part may not need either one. Any screen it leaves alone, such as
the simple setup screen or the mods screen, could run the AI's requests, if a screen
keeps running while another one is open. A small test would show that.

If that works, both mods could be on together. Really Advanced Setup keeps its choices
in its own settings store and copies them into the game's setup when the game starts.
When it is on, our tools could write to that store too, so the AI could also pick its
options (start bias, starting bonuses and units, extra resources and wonders).

## Open questions

- Can the AI's requests run from a screen that Really Advanced Setup leaves alone?
- Can a mod's Lua load a save file?
