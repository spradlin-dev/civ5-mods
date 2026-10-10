#include "CvGameCoreDLLPCH.h"
#include "ClaudePopups.h"
#include "ClaudeToolSupport.h"
#include "ClaudeSeat.h"
#include "../CvGame.h"
#include "../CvPlayer.h"
#include "../CvPlot.h"
#include "../CvMap.h"
#include "../CvGlobals.h"
#include "../CvTechClasses.h"
#include "../CvInfos.h"

using namespace ClaudeTool;

const char* ClaudePopupDialogToClose(int iType)
{
	switch(iType)
	{
	case BUTTONPOPUP_GOODY_HUT_REWARD: return "GoodyHutPopup";
	case BUTTONPOPUP_TECH_AWARD: return "TechAwardPopup";
	case BUTTONPOPUP_NATURAL_WONDER_REWARD: return "NaturalWonderPopup";
	case BUTTONPOPUP_NEW_ERA: return "NewEraPopup";
	case BUTTONPOPUP_WONDER_COMPLETED_ACTIVE_PLAYER: return "WonderPopup";
	case BUTTONPOPUP_BARBARIAN_CAMP_REWARD: return "BarbarianCampPopup";
	case BUTTONPOPUP_GOLDEN_AGE_REWARD: return "GoldenAgePopup";
	case BUTTONPOPUP_GREAT_PERSON_REWARD: return "GreatPersonRewardPopup";
	case BUTTONPOPUP_CITY_STATE_GREETING: return "CityStateGreetingPopup";
	case BUTTONPOPUP_TEXT: return "TextPopup";
	case BUTTONPOPUP_ADVISOR_COUNSEL: return "AdvisorCounselPopup";
	case BUTTONPOPUP_ADVISOR_INFO: return "AdvisorInfoPopup";
	default: return "";
	}
}

std::string ClaudeDescribePopup(int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text)
{
	switch(iType)
	{
	case BUTTONPOPUP_GOODY_HUT_REWARD:
		return text.empty() ? std::string("Ancient ruins explored") : "Ancient ruins explored: " + text;
	case BUTTONPOPUP_TECH_AWARD:
	{
		CvTechEntry* t = (iData3 >= 0) ? GC.getTechInfo((TechTypes)iData3) : NULL;
		return t ? ClaudeFormat("Researched %s: ", t->GetDescription()) + Localized(t->GetHelp()) : "Researched a technology";
	}
	case BUTTONPOPUP_NATURAL_WONDER_REWARD:
	{
		CvPlot* p = GC.getMap().plot(iData1, iData2);
		std::string s = ClaudeFormat("Discovered a natural wonder%s at (%d,%d)", (p && p->getFeatureType() != NO_FEATURE) ? ClaudeFormat(" (%s)", FeatureName(p->getFeatureType())).c_str() : "", iData1, iData2);
		if(bOption1) s += ", first to find it";
		if(iData3 > 0) s += ClaudeFormat(", +%d gold", iData3);
		return s;
	}
	case BUTTONPOPUP_NEW_ERA:
		return ClaudeFormat("Entered the %s", EraName((EraTypes)iData1));
	case BUTTONPOPUP_WONDER_COMPLETED_ACTIVE_PLAYER:
		return ClaudeFormat("Completed the wonder %s", BuildingName((BuildingTypes)iData1));
	case BUTTONPOPUP_BARBARIAN_CAMP_REWARD:
		return ClaudeFormat("Cleared a barbarian camp: +%d gold", iData1);
	case BUTTONPOPUP_GOLDEN_AGE_REWARD:
		return "A Golden Age has begun";
	case BUTTONPOPUP_GREAT_PERSON_REWARD:
		return ClaudeFormat("A %s has appeared", UnitTypeName((UnitTypes)iData1));
	case BUTTONPOPUP_CITY_STATE_GREETING:
	case BUTTONPOPUP_CITY_STATE_MESSAGE:
	{
		std::string who = (iData1 >= 0 && iData1 < MAX_CIV_PLAYERS) ? CivName((PlayerTypes)iData1) : "a city-state";
		if(iType == BUTTONPOPUP_CITY_STATE_MESSAGE)
			return who + ": " + text + " (their diplomacy screen is open in the game; a human must close it)";
		std::string s = "Met the city-state of " + who;
		if(iData2 > 0) s += ClaudeFormat(" (they gifted %d gold)", iData2);
		return s;
	}
	case BUTTONPOPUP_CITY_CAPTURED:
	{
		PlayerTypes eMe = ClaudeSeat();
		CvCity* c = (eMe != NO_PLAYER) ? GET_PLAYER(eMe).getCity(iData1) : NULL;
		std::string s = ClaudeFormat("Captured %s (city id %d)", c ? c->getName().c_str() : "a city", iData1);
		if(iData2 > 0) s += ClaudeFormat(", plundering %d gold", iData2);
		if(iData3 > 0) s += ClaudeFormat(" and %d culture", iData3);
		s += ". Decide its fate with city_task: annex, puppet, raze";
		PlayerTypes eOrig = c ? c->getOriginalOwner() : NO_PLAYER;
		if(c && eOrig != NO_PLAYER && eOrig != eMe && GET_PLAYER(eMe).CanLiberatePlayerCity(eOrig)) s += " or liberate (" + CivName(eOrig) + ")";
		return s;
	}
	case BUTTONPOPUP_ADVISOR_COUNSEL:
	case BUTTONPOPUP_ADVISOR_INFO:
		return "Advisor: " + Localized(text.c_str());
	case BUTTONPOPUP_TEXT:
		return Localized(text.c_str());
	default:
		return "";
	}
}
