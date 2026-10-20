# Local MCP Server DLL changelog

Versions are the mod's version numbers as the game shows them. A number missing from the list was a build that never left this PC; its changes are in the next version listed.

## Version 43 (2026-10-19)

- A model that takes a computer slot before anyone has moved now starts with a human player's units; the extra starting units the AI gets on harder levels are removed

## Version 42 (2026-10-19)

- Asking a computer player for a Declaration of Friendship now returns the leader's answer in words (too soon, asked recently, yes or no), the same text a human player hears, in the game's language

## Version 41 (2026-10-19)

- A Declaration of Friendship can no longer be put in a deal with a computer player (the game only allows that between humans); it is asked for in conversation, with the same waiting rules a human has

## Version 40 (2026-10-18)

- Add the map_image tool: a strategic-view style picture of the tiles around a point
- Enter in the log window's chat box no longer also ends your turn

## Version 39 (2026-10-18)

- Remove the elimination test aid (the log window's test link and the test tool)

## Version 38 (2026-10-18)

- Release the playback pause again whenever the engine re-takes it

## Version 37 (2026-10-18)

- Glide the camera again from a Go to link

## Version 36 (2026-10-18)

- Show seats to the engine as computer players while the human watches on

## Version 35 (2026-10-18)

- Jump the camera immediately from a Go to link

## Version 34 (2026-10-18)

- Keep playback live while watching after elimination

## Version 33 (2026-10-18)

- Watch on from an observer slot after elimination, and hand the test civ to a teammate

## Version 32 (2026-10-18)

- Add a test aid that eliminates the human player

## Version 31 (2026-10-18)

- Move the resource tooltip fix into its own mod

## Version 30 (2026-10-18)

- Pad the resource icon tooltip instead of moving it
- Shorten the token note in the Workshop description

## Version 28 (2026-10-18)

- Move the resource icon tooltip clear of the busy cursor

## Version 27 (2026-10-18)

- Move the tile tooltip clear of the busy cursor

## Version 26 (2026-10-18)

- Show tile coordinates in the tile info box above the minimap

## Version 25 (2026-10-18)

- Keep a captured city known to teams that had it under fog

## Version 24 (2026-10-18)

- Play seat turns live, name whose unit blocks a tile, list city buildings, explain dropped moves

## Version 13 (2026-10-18)

- Tell the model which offers an accepted deal made pointless

## Version 12 (2026-10-18)

- Use the game's chat ding instead of a custom sound
- Drop stale offers, and say when the game cancels a move

## Version 10 (2026-10-18)

- Say exactly why an attack or a move was refused

## Version 9 (2026-10-17)

- Color chat lines, chime on a reply, and show the AI's turn banner

## Version 5 (2026-10-17)

- Let the human chat with a teammate AI from the log window

## Version 4 (2026-10-17)

- Add Go to links under log lines that name tiles

## Version 3 (2026-10-15)

- Save the game setup plan
- Tell players a long game needs plenty of AI tokens

## Version 2 (2026-10-14)

- Keep the log add-in at the mod root so the game can find it

## Version 1 (2026-10-14)

- Let the AI hand scouts and workers to the game's automation, and start versioning the mod

## Before versions were numbered (2026-10-10 to 2026-10-14)

- Import Civ5 SDK game core source (Brave New World DLL)
- Build the BNW DLL and load it as the "Claude Player" mod
- Add the MCP bridge: Claude plays the active human player
- Round 2: briefing, actions, modal game messages
- Add the Claude seat, diplomacy and the rest of the game surface
- Fix save loading, the deal-preview crash, and add crash reporting
- Add citizen management, aircraft actions and trade route previews
- Refuse one-time unit actions when the unit has no moves left
- Rename the mod to ClaudePlayer and save the Workshop description draft
- Open the Claude Log window in the top-right corner
- Make the Claude Log readable and filterable
- Fix the Claude Log window failing to load
- Group each kind of unit under its own Claude Log filter
- Let the AI player name itself, and summarize each turn
- Hide the AI's log when it plays against you, and tidy its layout
- Log only crashes the Claude Player DLL is involved in
- Have the AI say who it is on connecting, and offer a secret opponent
- Rename the mod to Local MCP Server DLL
- Give each game an ID the AI can file its memories under
- Tell the AI which mods are loaded in the game
- Fix the AI Log script failing to load, and describe each mod
- Tell the AI how the game was set up
- Send the game's settings and mods to the AI automatically
- Say why a purchase was refused
- Record the plans for a rankings tool and several AI players
- Add a rankings tool showing where the AI's civilization stands
- Mark the rankings plan as built
- Let siege units set up before firing
- Refuse an attack from a unit with no moves left
- Ask for production as soon as a city is founded
- Tell the AI at once when a leader's request arrives
- Say what a loaded game means in the Workshop text
- Let several AI models play one game, each on its own seat
- Keep the game running after the human is eliminated
- Describe several AIs and watching after elimination in the Workshop text
- Let aircraft attack
- Keep a seat's deal offer visible to the human and restore log order
- Explain every refusal: builds, buys, rest, moves and upgrades
- Tell the AI whose borders each of its units stands inside
- Name the enemy or city an attack is aimed at in the log
- Save a plan for a map image tool
- Add the game's overview screens as tools
- Let move_unit accept preview_only
- Drop the Claude name from what players see and build the mod with ModBuddy
