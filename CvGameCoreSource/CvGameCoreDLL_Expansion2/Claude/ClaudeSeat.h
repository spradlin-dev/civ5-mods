#pragma once

#include <string>

// Game thread only: reads the game's slot data without a lock.

// Which player Claude plays: the human slot named "Claude", which take_seat
// makes out of a computer slot in a shared game, or else the active human. A
// claimed seat is never the player on screen, so the game's popups for it are
// recorded for Claude instead of shown.
PlayerTypes ClaudeSeat();
bool ClaudeSeatOnScreen();

// Game thread. Makes a computer-controlled civilization Claude's seat, or gives
// the claimed seat back to the game's AI. err names the reason on failure.
bool ClaudeClaimSeat(PlayerTypes ePlayer, std::string& err);
bool ClaudeReleaseSeat(std::string& err);

// The connected model's name, e.g. "Claude Opus 5.5"; "" until it has named itself.
std::string ClaudeIdentityLabel();
// Game thread. Shows that name after the claimed seat's leader, e.g. "Dido (Claude Opus 5.5)",
// or just the leader while no model has named itself.
void ClaudeUpdateSeatLabel();

// Game thread. Whether the player on screen may read the AI player's log: when the AI plays the
// on-screen civilization or one on its team, not when it is an opponent.
bool ClaudeLogVisibleOnScreen();

// Game thread. Records a popup the game only shows to the player on screen, when
// it is for Claude's off-screen seat (or that seat's team). Same arguments as the
// game's popup info.
void ClaudeNoteSeatPopup(PlayerTypes ePlayer, int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text);
// Game thread. Echoes a notification for Claude's seat into the Claude Log.
void ClaudeNoteSeatNotification(PlayerTypes ePlayer, const char* szMessage, const char* szSummary);
void ClaudeNoteSeatTeamPopup(TeamTypes eTeam, int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text);
