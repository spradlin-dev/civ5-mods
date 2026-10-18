# Plan: several AI players in one game

Phase 1 built on 2026-10-13: sessions, seats per model, per-seat stores, the log rules
below. Not built yet: watching after you are eliminated.

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

## Log rule

Log rule: lines written while the model's civilization was on your team stay readable.
Lines written while it was your opponent are never shown, even after that model moves to
a teammate's civilization.
