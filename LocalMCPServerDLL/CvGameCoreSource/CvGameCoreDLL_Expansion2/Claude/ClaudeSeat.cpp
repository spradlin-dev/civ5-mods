#include "CvGameCoreDLLPCH.h"
#pragma comment(lib, "ole32.lib")
// From objbase.h, which does not compile under the precompiled header's trimmed Windows headers.
extern "C" __declspec(dllimport) HRESULT __stdcall CoCreateGuid(GUID* pguid);
#include "ClaudeSeat.h"
#include "ClaudeBridge.h"
#include "ClaudeDiplomacy.h"
#include "ClaudeLogText.h"
#include "ClaudePopups.h"
#include "ClaudeToolSupport.h"
#include "../CvGame.h"
#include "../CvPlayer.h"
#include "../CvUnit.h"
#include "../CvCity.h"
#include "../CvTypes.h"
#include "../CvGlobals.h"

namespace
{
	const char* GAME_ID_PREFIX = "civ5-game-";
	const char* LEGACY_SEAT_NAME = "Claude"; // saves from before models named themselves
	const char* NOT_AVAILABLE = "that civilization is not available";

	// The log is readable on screen, so an opponent's civilization is not named in it.
	std::string SeatNameForScreen(PlayerTypes e)
	{
		return ClaudeOnScreenTeam(e) ? GET_PLAYER(e).getCivilizationShortDescription() : "an opponent civilization";
	}

	PlayerTypes ActiveHuman()
	{
		PlayerTypes eActive = GC.getGame().getActivePlayer();
		return (eActive != NO_PLAYER && GET_PLAYER(eActive).isHuman()) ? eActive : NO_PLAYER;
	}

	// A civilization some model plays: a seat, or the one on screen when a session plays it.
	bool PlayedByModel(PlayerTypes e)
	{
		return ClaudeIsSeat(e) || (e != NO_PLAYER && e == GC.getGame().getActivePlayer() && ClaudeBridge::Get().ScreenPlayed());
	}

	std::string Nickname(PlayerTypes e)
	{
		return CvPreGame::nickname(e).c_str();
	}

	// Binds the seat to the current session and announces it in the seat's own log.
	void Bind(PlayerTypes ePlayer, const std::string& label, const std::string& what)
	{
		ClaudeBridge& bridge = ClaudeBridge::Get();
		bridge.UnbindSeat(ePlayer);
		bridge.SetCurrentSeat(ePlayer);
		ClaudeBindSeatLabel(ePlayer, label);
		bridge.LogLineFor(ePlayer, CLAUDE_LOG_TURN, label + " " + what + " " + SeatNameForScreen(ePlayer));
	}
}

bool ClaudeIsSeat(PlayerTypes e)
{
	// In a single-player game the only human slot besides the one on screen is a seat.
	CvGame& game = GC.getGame();
	if(game.isNetworkMultiPlayer() || game.isHotSeat() || game.isPbem()) return false;
	return e != NO_PLAYER && e < MAX_MAJOR_CIVS && e != game.getActivePlayer() && CvPreGame::slotStatus(e) == SS_TAKEN;
}

bool ClaudeAnySeat()
{
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		if(ClaudeIsSeat((PlayerTypes)i)) return true;
	return false;
}

std::vector<PlayerTypes> ClaudeUnboundSeats()
{
	std::vector<PlayerTypes> out;
	ClaudeSession s;
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		if(ClaudeIsSeat((PlayerTypes)i) && !ClaudeBridge::Get().SessionForSeat(i, s)) out.push_back((PlayerTypes)i);
	return out;
}

bool ClaudeSeatLabelMatches(PlayerTypes e, const std::string& label)
{
	if(label.empty()) return false;
	std::string nick = Nickname(e);
	if(nick == label) return true;
	return nick == LEGACY_SEAT_NAME && label.compare(0, strlen(LEGACY_SEAT_NAME), LEGACY_SEAT_NAME) == 0;
}

std::string ClaudeSeatWaitingText(PlayerTypes e, const std::string& label)
{
	std::string last = Nickname(e);
	std::string by = last.empty() ? "" : " (last played by " + last + ")";
	if(ClaudeSeatLabelMatches(e, label)) return std::string(GET_PLAYER(e).getCivilizationShortDescription()) + " (yours: take it back with take_seat)";
	if(ClaudeOnScreenTeam(e)) return std::string(GET_PLAYER(e).getCivilizationShortDescription()) + by;
	return "an opponent civilization" + by + ": only the model that played it, or take_seat civ=opponent, can take it";
}

PlayerTypes ClaudePlayerOf(const ClaudeSession& s)
{
	if(s.seat >= 0) return ClaudeIsSeat((PlayerTypes)s.seat) ? (PlayerTypes)s.seat : NO_PLAYER;
	return s.seat == CLAUDE_SEAT_SCREEN ? ActiveHuman() : NO_PLAYER;
}

PlayerTypes ClaudeSeat()
{
	return ClaudePlayerOf(ClaudeBridge::Get().Current());
}

bool ClaudeSeatOnScreen()
{
	return ClaudeBridge::Get().Current().seat == CLAUDE_SEAT_SCREEN;
}

bool ClaudeClaimSeat(PlayerTypes ePlayer, std::string& err, bool bAssignedOpponent)
{
	CvGame& game = GC.getGame();
	if(game.isNetworkMultiPlayer() || game.isHotSeat() || game.isPbem()) { err = "seats can only be claimed in a single-player game"; return false; }
	ClaudeBridge& bridge = ClaudeBridge::Get();
	ClaudeSession mine = bridge.Current();
	if(mine.seat >= 0 && ClaudeIsSeat((PlayerTypes)mine.seat))
	{
		err = std::string("already playing ") + GET_PLAYER((PlayerTypes)mine.seat).getCivilizationShortDescription() + "; call release_seat first";
		return false;
	}
	if(ePlayer == NO_PLAYER || ePlayer >= MAX_MAJOR_CIVS) { err = "that is not a major civilization"; return false; }
	CvPlayer& p = GET_PLAYER(ePlayer);
	if(!p.isAlive()) { err = "that civilization is not alive"; return false; }
	std::string label = ClaudeCurrentLabel();
	if(ClaudeIsSeat(ePlayer))
	{
		// A seat another model left: the model's own comes back to it; one on the on-screen
		// team the human may hand to any model; an opponent's stays secret.
		ClaudeSession other;
		if(bridge.SessionForSeat(ePlayer, other)) { err = NOT_AVAILABLE; return false; }
		if(!ClaudeSeatLabelMatches(ePlayer, label) && !ClaudeOnScreenTeam(ePlayer) && !bAssignedOpponent) { err = NOT_AVAILABLE; return false; }
		Bind(ePlayer, label, "is back on");
		return true;
	}
	if(p.isHuman()) { err = "that civilization is played by the human on screen"; return false; }
	if(p.isTurnActive()) { err = "that civilization is taking its turn right now; claim it during the human's turn"; return false; }

	CvPreGame::setSlotStatus(ePlayer, SS_TAKEN);
	PlayerTypes eActive = game.getActivePlayer();
	if(eActive != NO_PLAYER) CvPreGame::setHandicap(ePlayer, CvPreGame::handicap(eActive));
	Bind(ePlayer, label, "now plays");
	return true;
}

bool ClaudeReleaseSeat(std::string& err)
{
	ClaudeBridge& bridge = ClaudeBridge::Get();
	ClaudeSession mine = bridge.Current();
	PlayerTypes e = (mine.seat >= 0) ? (PlayerTypes)mine.seat : NO_PLAYER;
	if(e == NO_PLAYER || !ClaudeIsSeat(e)) { err = "you hold no seat"; return false; }
	if(GET_PLAYER(e).isTurnActive()) { err = "end your turn first; the seat can be released while another player's turn is running"; return false; }
	bridge.LogLineFor(e, CLAUDE_LOG_TURN, "The game's AI now plays " + SeatNameForScreen(e));
	CvPreGame::setSlotStatus(e, SS_COMPUTER);
	CvPreGame::setNickname(e, CvString(""));
	CvPreGame::setLeaderName(e, CvString(""));
	CvPreGame::setHandicap(e, (HandicapTypes)GC.getAI_HANDICAP());
	ClaudeDropDiploRequestsFor(e);
	bridge.SetCurrentSeat(CLAUDE_SEAT_UNDECIDED);
	return true;
}

PlayerTypes ClaudeWaitingOpponentSeat()
{
	std::vector<PlayerTypes> waiting = ClaudeUnboundSeats();
	PlayerTypes eFound = NO_PLAYER;
	for(size_t i = 0; i < waiting.size(); ++i)
	{
		if(ClaudeOnScreenTeam(waiting[i])) continue;
		if(eFound != NO_PLAYER) return NO_PLAYER;
		eFound = waiting[i];
	}
	return eFound;
}

// The team of the human eliminated while the screen watches on; not kept across a reload.
static TeamTypes s_eliminatedTeam = NO_TEAM;

bool ClaudeOnScreenTeam(PlayerTypes e)
{
	PlayerTypes eActive = GC.getGame().getActivePlayer();
	if(eActive == NO_PLAYER) return true;
	TeamTypes eScreen = CvPreGame::slotStatus(eActive) == SS_OBSERVER && s_eliminatedTeam != NO_TEAM ? s_eliminatedTeam : GET_PLAYER(eActive).getTeam();
	return GET_PLAYER(e).getTeam() == eScreen;
}

bool ClaudeKeepDeadSlotOpen(PlayerTypes eDead)
{
	CvGame& game = GC.getGame();
	if(eDead != game.getActivePlayer() || CvPreGame::slotStatus(eDead) != SS_TAKEN || !ClaudeAnySeatAlive()) return false;
	s_eliminatedTeam = GET_PLAYER(eDead).getTeam();
	ClaudeBridge::Get().LogLineFor(-1, CLAUDE_LOG_ALWAYS, "Your civilization has been eliminated. The game goes on so the AI players can finish it; you can watch to the end.");
	if(game.CanMoveActivePlayerToObserver())
	{
		game.ActivateObserverSlot();
		return false;
	}
	CvPreGame::setSlotStatus(eDead, SS_OBSERVER);
	return true;
}

std::string ClaudeNewUuid()
{
	GUID g;
	if(FAILED(CoCreateGuid(&g))) return "";
	char id[64];
	_snprintf_s(id, sizeof(id), _TRUNCATE, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	            g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
	return id;
}

std::string ClaudeGameId()
{
	std::string name = CvPreGame::gameName().c_str();
	if(name.compare(0, strlen(GAME_ID_PREFIX), GAME_ID_PREFIX) == 0) return name;
	std::string uuid = ClaudeNewUuid();
	if(uuid.empty()) return "";
	std::string id = GAME_ID_PREFIX + uuid;
	CvPreGame::setGameName(CvString(id));
	return id;
}

std::string ClaudeIdentityLabel(const ClaudeIdentity& id)
{
	std::string label;
	const std::string* parts[] = { &id.provider, &id.model, &id.version };
	for(int i = 0; i < 3; ++i)
	{
		if(parts[i]->empty()) continue;
		if(!label.empty()) label += " ";
		label += *parts[i];
	}
	return label;
}

std::string ClaudeCurrentLabel()
{
	return ClaudeIdentityLabel(ClaudeBridge::Get().Current().identity);
}

std::string ClaudeSeatLabel(PlayerTypes e)
{
	if(e == NO_PLAYER) return "";
	ClaudeSession s;
	bool bSeat = ClaudeIsSeat(e);
	if(bSeat ? ClaudeBridge::Get().SessionForSeat(e, s) : (e == GC.getGame().getActivePlayer() && ClaudeBridge::Get().SessionForSeat(CLAUDE_SEAT_SCREEN, s)))
	{
		std::string label = ClaudeIdentityLabel(s.identity);
		if(!label.empty()) return label;
	}
	return bSeat ? Nickname(e) : "";
}

void ClaudeBindSeatLabel(PlayerTypes e, const std::string& label)
{
	if(!ClaudeIsSeat(e)) return;
	CvPreGame::setNickname(e, CvString(label));
	CvPreGame::setLeaderName(e, CvString(label.empty() ? std::string() : std::string(GET_PLAYER(e).getLeaderInfo().GetDescription()) + " (" + label + ")"));
}

void ClaudeNoteSeatPopup(PlayerTypes ePlayer, int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text)
{
	if(!ClaudeIsSeat(ePlayer)) return; // the on-screen player's popups arrive through the UI
	ClaudeBridge::Get().NotePopup(ePlayer, iType, iData1, iData2, iData3, bOption1, text, false);
}

void ClaudeNoteSeatNotification(PlayerTypes ePlayer, const char* szMessage, const char* szSummary)
{
	if(!PlayedByModel(ePlayer)) return;
	const char* szText = (szMessage && *szMessage) ? szMessage : szSummary;
	if(!szText || !*szText) return;
	// City-state quest notifications end with the game's "RIGHT-CLICK to dismiss" hint, meant for the notification panel.
	std::string text = szText;
	std::string dismiss = Localization::Lookup("TXT_KEY_MINOR_QUEST_BLOCKING_TT").toUTF8();
	size_t at = text.find(dismiss);
	if(at != std::string::npos) text.erase(at);
	ClaudeBridge::Get().LogLineFor(ePlayer, CLAUDE_LOG_NOTIFICATIONS, "Notification: " + text);
}

void ClaudeNoteOrdersCancelled(CvUnit* u, const char* szWhy)
{
	if(!u || !ClaudeIsSeat(u->getOwner())) return;
	const MissionData* m = u->GetHeadMissionData();
	if(!m || (m->eMissionType != CvTypes::getMISSION_MOVE_TO() && m->eMissionType != CvTypes::getMISSION_ROUTE_TO())) return;
	CvUnitEntry* info = GC.getUnitInfo(u->getUnitType());
	std::string text = ClaudeFormat("Your %s (id %d) at (%d,%d) will not continue to (%d,%d): %s", info ? info->GetDescription() : "unit", u->GetID(), u->getX(), u->getY(), m->iData1, m->iData2, szWhy);
	ClaudeBridge::Get().NotePopup(u->getOwner(), CLAUDE_POPUP_TEXT, 0, 0, 0, false, text, false);
}

std::vector<PlayerTypes> ClaudeTeammateSeats()
{
	std::vector<PlayerTypes> out;
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
	{
		PlayerTypes e = (PlayerTypes)i;
		if(ClaudeIsSeat(e) && ClaudeOnScreenTeam(e) && GET_PLAYER(e).isAlive()) out.push_back(e);
	}
	return out;
}

void ClaudeNoteSeatTeamPopup(TeamTypes eTeam, int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text)
{
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		if(ClaudeIsSeat((PlayerTypes)i) && GET_PLAYER((PlayerTypes)i).getTeam() == eTeam)
			ClaudeNoteSeatPopup((PlayerTypes)i, iType, iData1, iData2, iData3, bOption1, text);
}

bool ClaudeAnySeatAlive()
{
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		if(ClaudeIsSeat((PlayerTypes)i) && GET_PLAYER((PlayerTypes)i).isAlive()) return true;
	return false;
}

bool ClaudeScreenIsObserver()
{
	PlayerTypes eActive = GC.getGame().getActivePlayer();
	return eActive != NO_PLAYER && CvPreGame::slotStatus(eActive) == SS_OBSERVER;
}

bool ClaudeEngineSeesAsHuman(PlayerTypes e)
{
	return !(ClaudeIsSeat(e) && ClaudeScreenIsObserver());
}

static volatile bool s_eliminateRequested = false;

bool ClaudeEliminateOnScreenPlayer(std::string& err)
{
	PlayerTypes eActive = GC.getGame().getActivePlayer();
	if(eActive == NO_PLAYER || !GET_PLAYER(eActive).isHuman() || ClaudeIsSeat(eActive)) { err = "no human player is on screen"; return false; }
	if(!GET_PLAYER(eActive).isAlive()) { err = "the player on screen has already been eliminated"; return false; }
	if(!ClaudeAnySeatAlive()) { err = "no AI seat is alive to keep the game going, so the game would simply end"; return false; }
	CvPlayer& dying = GET_PLAYER(eActive);
	std::vector<PlayerTypes> heirs = ClaudeTeammateSeats();
	if(!heirs.empty())
	{
		CvPlayer& heir = GET_PLAYER(heirs[0]);
		int iLoop = 0;
		std::vector<int> ids;
		// Units first: giving away the last city kills the civilization, units and all.
		for(CvUnit* u = dying.firstUnit(&iLoop); u; u = dying.nextUnit(&iLoop)) ids.push_back(u->GetID());
		// The steps of CvUnit::gift, which only gives to the owner of the unit's tile.
		for(size_t i = 0; i < ids.size(); ++i)
		{
			CvUnit* u = dying.getUnit(ids[i]);
			if(!u) continue;
			CvUnit* pNew = heir.initUnit(u->getUnitType(), u->getX(), u->getY(), u->AI_getUnitAIType(), NO_DIRECTION, false, false);
			if(!pNew) continue;
			pNew->convert(u, false);
			pNew->setupGraphical();
			pNew->GetReligionData()->SetReligion(u->GetReligionData()->GetReligion());
			pNew->GetReligionData()->SetReligiousStrength(u->GetReligionData()->GetReligiousStrength());
			pNew->GetReligionData()->SetSpreadsLeft(u->GetReligionData()->GetSpreadsLeft());
		}
		ids.clear();
		for(CvCity* c = dying.firstCity(&iLoop); c; c = dying.nextCity(&iLoop)) ids.push_back(c->GetID());
		for(size_t i = 0; i < ids.size(); ++i)
			if(CvCity* c = dying.getCity(ids[i])) heir.acquireCity(c, false, true);
	}
	// Without an heir, or a city to lose: bNotify false, as the game's own elimination passes it.
	dying.setAlive(false, false);
	return true;
}

void ClaudeRequestEliminateOnScreenPlayer()
{
	s_eliminateRequested = true;
}

void ClaudeRunEliminationRequest()
{
	if(!s_eliminateRequested) return;
	s_eliminateRequested = false;
	std::string err;
	if(!ClaudeEliminateOnScreenPlayer(err)) ClaudeBridge::Get().LogLineFor(-1, CLAUDE_LOG_ALWAYS, "Not eliminated: " + err);
}
