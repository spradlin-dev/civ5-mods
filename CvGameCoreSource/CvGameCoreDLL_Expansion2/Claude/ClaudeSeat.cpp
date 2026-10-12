#include "CvGameCoreDLLPCH.h"
#pragma comment(lib, "ole32.lib")
// From objbase.h, which does not compile under the precompiled header's trimmed Windows headers.
extern "C" __declspec(dllimport) HRESULT __stdcall CoCreateGuid(GUID* pguid);
#include "ClaudeSeat.h"
#include "ClaudeBridge.h"
#include "ClaudeLogText.h"
#include "../CvGame.h"
#include "../CvPlayer.h"
#include "../CvGlobals.h"

namespace
{
	const char* SEAT_NAME = "Claude";
	const char* GAME_ID_PREFIX = "civ5-game-";

	bool NameIsSeat(const CvString& name)
	{
		std::string s(name.c_str());
		size_t b = s.find_first_not_of(" \t");
		if(b == std::string::npos) return false;
		s = s.substr(b, s.find_last_not_of(" \t") - b + 1);
		return _stricmp(s.c_str(), SEAT_NAME) == 0;
	}

	// The log is readable on screen, so an opponent's civilization is not named in it.
	std::string SeatNameForScreen(PlayerTypes e)
	{
		return ClaudeOnScreenTeam(e) ? GET_PLAYER(e).getCivilizationShortDescription() : "an opponent civilization";
	}

	PlayerTypes NamedSeat()
	{
		PlayerTypes eActive = GC.getGame().getActivePlayer();
		for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		{
			PlayerTypes e = (PlayerTypes)i;
			if(e != eActive && CvPreGame::slotStatus(e) == SS_TAKEN && NameIsSeat(CvPreGame::nickname(e))) return e;
		}
		return NO_PLAYER;
	}
}

PlayerTypes ClaudeSeat()
{
	PlayerTypes e = NamedSeat();
	if(e != NO_PLAYER) return e;
	PlayerTypes eActive = GC.getGame().getActivePlayer();
	return (eActive != NO_PLAYER && GET_PLAYER(eActive).isHuman()) ? eActive : NO_PLAYER;
}

bool ClaudeSeatOnScreen()
{
	PlayerTypes e = ClaudeSeat();
	return e != NO_PLAYER && e == GC.getGame().getActivePlayer();
}

bool ClaudeClaimSeat(PlayerTypes ePlayer, std::string& err)
{
	CvGame& game = GC.getGame();
	if(game.isNetworkMultiPlayer() || game.isHotSeat() || game.isPbem()) { err = "seats can only be claimed in a single-player game"; return false; }
	PlayerTypes eNamed = NamedSeat();
	if(eNamed != NO_PLAYER)
	{
		err = std::string("already playing ") + GET_PLAYER(eNamed).getCivilizationShortDescription() + "; call release_seat first";
		return false;
	}
	if(ePlayer == NO_PLAYER || ePlayer >= MAX_MAJOR_CIVS) { err = "that is not a major civilization"; return false; }
	CvPlayer& p = GET_PLAYER(ePlayer);
	if(!p.isAlive()) { err = "that civilization is not alive"; return false; }
	if(p.isHuman()) { err = "that civilization is already played by a human"; return false; }
	if(p.isTurnActive()) { err = "that civilization is taking its turn right now; claim it during the human's turn"; return false; }

	CvPreGame::setSlotStatus(ePlayer, SS_TAKEN);
	CvPreGame::setNickname(ePlayer, CvString(SEAT_NAME));
	PlayerTypes eActive = game.getActivePlayer();
	if(eActive != NO_PLAYER) CvPreGame::setHandicap(ePlayer, CvPreGame::handicap(eActive));
	ClaudeUpdateSeatLabel();
	ClaudeBridge::Get().LogLine(CLAUDE_LOG_TURN, ClaudeIdentityLabel() + " now plays " + SeatNameForScreen(ePlayer) + " at the human's difficulty");
	return true;
}

bool ClaudeReleaseSeat(std::string& err)
{
	PlayerTypes e = NamedSeat();
	if(e == NO_PLAYER) { err = "no seat is claimed"; return false; }
	if(GET_PLAYER(e).isTurnActive()) { err = "end your turn first; the seat can be released while another player's turn is running"; return false; }
	CvPreGame::setSlotStatus(e, SS_COMPUTER);
	CvPreGame::setNickname(e, CvString(""));
	CvPreGame::setLeaderName(e, CvString(""));
	CvPreGame::setHandicap(e, (HandicapTypes)GC.getAI_HANDICAP());
	ClaudeBridge::Get().LogLine(CLAUDE_LOG_TURN, "The game's AI now plays " + SeatNameForScreen(e));
	return true;
}

bool ClaudeOnScreenTeam(PlayerTypes e)
{
	PlayerTypes eActive = GC.getGame().getActivePlayer();
	return eActive == NO_PLAYER || GET_PLAYER(e).getTeam() == GET_PLAYER(eActive).getTeam();
}

bool ClaudeLogVisibleOnScreen()
{
	PlayerTypes eSeat = ClaudeSeat();
	return eSeat == NO_PLAYER || ClaudeOnScreenTeam(eSeat);
}

std::string ClaudeGameId()
{
	std::string name = CvPreGame::gameName().c_str();
	if(name.compare(0, strlen(GAME_ID_PREFIX), GAME_ID_PREFIX) == 0) return name;
	GUID g;
	if(FAILED(CoCreateGuid(&g))) return "";
	char id[64];
	_snprintf_s(id, sizeof(id), _TRUNCATE, "%s%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", GAME_ID_PREFIX,
	            g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
	CvPreGame::setGameName(CvString(id));
	return id;
}

std::string ClaudeIdentityLabel()
{
	ClaudeIdentity id = ClaudeBridge::Get().Identity();
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

void ClaudeUpdateSeatLabel()
{
	PlayerTypes e = NamedSeat();
	if(e == NO_PLAYER) return;
	std::string label = ClaudeIdentityLabel();
	CvPreGame::setLeaderName(e, CvString(label.empty() ? std::string() : std::string(GET_PLAYER(e).getLeaderInfo().GetDescription()) + " (" + label + ")"));
}

void ClaudeNoteSeatPopup(PlayerTypes ePlayer, int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text)
{
	PlayerTypes eSeat = NamedSeat();
	if(eSeat == NO_PLAYER || ePlayer != eSeat || eSeat == GC.getGame().getActivePlayer()) return;
	ClaudeBridge::Get().NotePopup(iType, iData1, iData2, iData3, bOption1, text, false);
}

void ClaudeNoteSeatNotification(PlayerTypes ePlayer, const char* szMessage, const char* szSummary)
{
	if(ePlayer == NO_PLAYER || ePlayer != ClaudeSeat()) return;
	const char* szText = (szMessage && *szMessage) ? szMessage : szSummary;
	if(!szText || !*szText) return;
	// City-state quest notifications end with the game's "RIGHT-CLICK to dismiss" hint, meant for the notification panel.
	std::string text = szText;
	std::string dismiss = Localization::Lookup("TXT_KEY_MINOR_QUEST_BLOCKING_TT").toUTF8();
	size_t at = text.find(dismiss);
	if(at != std::string::npos) text.erase(at);
	ClaudeBridge::Get().LogLine(CLAUDE_LOG_NOTIFICATIONS, "Notification: " + text);
}

void ClaudeNoteSeatTeamPopup(TeamTypes eTeam, int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text)
{
	PlayerTypes eSeat = NamedSeat();
	if(eSeat != NO_PLAYER && GET_PLAYER(eSeat).getTeam() == eTeam)
		ClaudeNoteSeatPopup(eSeat, iType, iData1, iData2, iData3, bOption1, text);
}
