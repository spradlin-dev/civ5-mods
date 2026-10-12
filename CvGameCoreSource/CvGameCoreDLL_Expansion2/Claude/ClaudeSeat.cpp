#include "CvGameCoreDLLPCH.h"
#include "ClaudeSeat.h"
#include "ClaudeBridge.h"
#include "ClaudeLogText.h"
#include "../CvGame.h"
#include "../CvPlayer.h"
#include "../CvGlobals.h"

namespace
{
	const char* SEAT_NAME = "Claude";

	bool NameIsSeat(const CvString& name)
	{
		std::string s(name.c_str());
		size_t b = s.find_first_not_of(" \t");
		if(b == std::string::npos) return false;
		s = s.substr(b, s.find_last_not_of(" \t") - b + 1);
		return _stricmp(s.c_str(), SEAT_NAME) == 0;
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
	ClaudeBridge::Get().LogLine(CLAUDE_LOG_TURN, std::string("Claude now plays ") + p.getCivilizationShortDescription() + " at the human's difficulty");
	return true;
}

bool ClaudeReleaseSeat(std::string& err)
{
	PlayerTypes e = NamedSeat();
	if(e == NO_PLAYER) { err = "no seat is claimed"; return false; }
	if(GET_PLAYER(e).isTurnActive()) { err = "end your turn first; the seat can be released while another player's turn is running"; return false; }
	CvPreGame::setSlotStatus(e, SS_COMPUTER);
	CvPreGame::setNickname(e, CvString(""));
	CvPreGame::setHandicap(e, (HandicapTypes)GC.getAI_HANDICAP());
	ClaudeBridge::Get().LogLine(CLAUDE_LOG_TURN, std::string("The game's AI now plays ") + GET_PLAYER(e).getCivilizationShortDescription());
	return true;
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
	if(szText && *szText) ClaudeBridge::Get().LogLine(CLAUDE_LOG_NOTIFICATIONS, std::string("Notification: ") + szText);
}

void ClaudeNoteSeatTeamPopup(TeamTypes eTeam, int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text)
{
	PlayerTypes eSeat = NamedSeat();
	if(eSeat != NO_PLAYER && GET_PLAYER(eSeat).getTeam() == eTeam)
		ClaudeNoteSeatPopup(eSeat, iType, iData1, iData2, iData3, bOption1, text);
}
