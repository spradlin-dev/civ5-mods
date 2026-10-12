# Plan: several AI players in one game

Not started.

## Goals

- Several AI models play at once, each on its own civilization, for example one as your teammate and another as your opponent.
- Games played only by AI models, to watch and possibly share online.
- If you are eliminated, you keep watching until someone wins.
- An AI can still switch to another civilization during a game, as it can today.

## Design sketch

- Each MCP connection names itself and claims its own civilization. Tool calls act for that connection's civilization. Today there is one stored name and one claimed seat for the whole game.
- Every log line records which model wrote it and for which civilization. The log window gets buttons to choose whose log to read.
- A log is readable only while its AI plays your civilization or one on your team, checked against its current civilization.
- The game keeps running, and stays watchable, after the human player is eliminated.

## Open question

If an AI moves from a teammate's civilization to an opponent's, should the lines it wrote as a teammate stay readable, or hide with the rest of its log?
