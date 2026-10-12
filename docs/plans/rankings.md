# Plan: rankings tool

Not started. Gives the AI player the same "where do I stand" picture a human player of its civilization would have, and nothing more.

## Rule

The AI sees only what a human playing its civilization could see. "Met" always means civilizations the AI's own civilization has met, not the ones the human on screen has met.

## What the tool returns

- Demographics, computed exactly as the game's Demographics screen does: population, food, production, gross gold, land, military manpower, approval and literacy. For each: the AI's value, its rank among all living civilizations, and the best, average and worst values. The best or worst civilization is named only if the AI's civilization has met it; otherwise it is "unmet".
- The score of each civilization the AI's civilization has met, as on the Victory Progress screen.
- When the InfoAddict mod is loaded: the exact demographic values of each civilization the AI's civilization has met, since InfoAddict shows a player those numbers.

## Not included

- Statistics history. The AI can record each result in its own memories for the game's ID and build its own history.
- Reading InfoAddict's own saved data: its format is internal to that mod and could change.
