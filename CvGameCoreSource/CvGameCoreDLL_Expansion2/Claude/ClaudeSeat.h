#pragma once

#include <string>
#include <vector>

struct ClaudeIdentity;
struct ClaudeSession;
class CvUnit;

// Game thread only: reads the game's slot data without a lock.

// A seat is a human slot that take_seat made out of a computer slot in a single-player
// game; several models can each hold one. A seat is never the player on screen, so the
// game's popups for it are recorded for its model instead of shown.
bool ClaudeIsSeat(PlayerTypes e);
bool ClaudeAnySeat();
// Seats no live session plays right now (a model that reconnects takes its seat back).
std::vector<PlayerTypes> ClaudeUnboundSeats();
// Whether a seat's saved label is this model's: the same label, or the "Claude" of saves
// made before models named themselves, for any Claude model.
bool ClaudeSeatLabelMatches(PlayerTypes e, const std::string& label);
// How a waiting seat is described to a model: by name when it is the model's own or on the
// on-screen team, otherwise only as an opponent civilization.
std::string ClaudeSeatWaitingText(PlayerTypes e, const std::string& label);

// The civilization a session plays: its seat, the active human for one playing the screen,
// NO_PLAYER for one that is undecided or whose seat is lost.
PlayerTypes ClaudePlayerOf(const ClaudeSession& s);
// ClaudePlayerOf the current session.
PlayerTypes ClaudeSeat();
// True when the current session plays the civilization on screen (it holds no seat).
bool ClaudeSeatOnScreen();

// Game thread. Makes a computer-controlled civilization the current session's seat, or
// takes back a seat no live session plays (the model's own, or one on the on-screen team
// that the human assigns it); or gives the session's seat back to the game's AI.
// err names the reason on failure without revealing which civilizations other models play.
// bAssignedOpponent: the seat was picked for the model by take_seat civ=opponent, which may
// hand over a waiting opponent seat another model left without the model naming it.
bool ClaudeClaimSeat(PlayerTypes ePlayer, std::string& err, bool bAssignedOpponent = false);
bool ClaudeReleaseSeat(std::string& err);
// The one opponent seat waiting for a player, if there is exactly one; NO_PLAYER otherwise.
PlayerTypes ClaudeWaitingOpponentSeat();

// Game thread. A UUID naming this game across reloads and connections, so a model can keep
// memories per game. Kept in the saved game name, which only multiplayer lobbies show;
// a game without one gets one when it starts or loads, and keeps it once saved.
std::string ClaudeGameId();
// A fresh UUID as text.
std::string ClaudeNewUuid();

// A model's name from its identity, e.g. "Claude Opus 5.5"; "" until it has named itself.
std::string ClaudeIdentityLabel(const ClaudeIdentity& id);
// The current session's label.
std::string ClaudeCurrentLabel();
// The label of the model playing a civilization: its live session's, or the last one saved
// with the seat; "" for a civilization no model plays.
std::string ClaudeSeatLabel(PlayerTypes e);
// Game thread. Saves the model's label with the seat and shows it after the seat's leader,
// e.g. "Dido (Claude Opus 5.5)".
void ClaudeBindSeatLabel(PlayerTypes e, const std::string& label);

// Game thread. True when e is the player on screen or on its team (also when nobody is on screen).
// After the human's elimination the screen is an observer; its team stays the eliminated one's.
bool ClaudeOnScreenTeam(PlayerTypes e);
// The living seats on the on-screen player's team: the ones the human can chat with.
std::vector<PlayerTypes> ClaudeTeammateSeats();

// Game thread, from CvPlayer::setAlive just before a dead player's slot is closed. When the
// player on screen dies while a seat lives on, the screen moves to a free observer slot the way
// the game's own AI autoplay does, so the engine never sees its own slot close (which shows the
// defeat screen and stops the map). True when the dead slot must stay open: no observer slot
// was free, so the dead slot itself became the observer.
bool ClaudeKeepDeadSlotOpen(PlayerTypes eDead);
// Whether any seat is still alive: what keeps the game going after the human's elimination.
bool ClaudeAnySeatAlive();

// Test aid. Eliminates the human player on screen so that the game going on after their
// elimination can be checked without losing a whole game: a teammate seat, if there is one,
// inherits their units and then their cities as gifts, and the civilization dies as its last
// city goes. Refuses, with the reason in err, unless a seat is alive to keep the game going.
bool ClaudeEliminateOnScreenPlayer(std::string& err);
// UI thread: asks for the above on the next game-thread tick; a refusal is logged.
void ClaudeRequestEliminateOnScreenPlayer();
// Game thread, every tick.
void ClaudeRunEliminationRequest();

// Game thread. Records a popup the game only shows to the player on screen, when
// it is for a seat (or that seat's team). Same arguments as the game's popup info.
void ClaudeNoteSeatPopup(PlayerTypes ePlayer, int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text);
// Game thread. Echoes a notification for a civilization a model plays into its log.
void ClaudeNoteSeatNotification(PlayerTypes ePlayer, const char* szMessage, const char* szSummary);
void ClaudeNoteSeatTeamPopup(TeamTypes eTeam, int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text);
// Game thread, just before the game drops a unit's queued move. A seat's model is told, as a
// game message, that the move will not continue and why; other owners' units are ignored.
void ClaudeNoteOrdersCancelled(CvUnit* u, const char* szWhy);
