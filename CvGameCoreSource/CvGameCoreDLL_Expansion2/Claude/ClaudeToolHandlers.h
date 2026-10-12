#pragma once

#include <string>
#include "picojson.h"

// Tool handlers, one per MCP tool, registered in ClaudeTools.cpp. All run on the game thread.
#define CLAUDE_TOOL(name) picojson::value name(const picojson::object& args, std::string& err)

// ClaudeTools.cpp
CLAUDE_TOOL(ToolStatus);
CLAUDE_TOOL(ToolOverview);
CLAUDE_TOOL(ToolUnits);
CLAUDE_TOOL(ToolUnitOptions);
CLAUDE_TOOL(ToolCities);
CLAUDE_TOOL(ToolCityOptions);
CLAUDE_TOOL(ToolMap);
CLAUDE_TOOL(ToolResearchOptions);
CLAUDE_TOOL(ToolMoveUnit);
CLAUDE_TOOL(ToolUnitAction);
CLAUDE_TOOL(ToolSetProduction);
CLAUDE_TOOL(ToolSetResearch);
CLAUDE_TOOL(ToolEndTurn);
CLAUDE_TOOL(ToolLog);
CLAUDE_TOOL(ToolAcknowledgeMessages);
CLAUDE_TOOL(ToolMods);
CLAUDE_TOOL(ToolSetIdentity);
CLAUDE_TOOL(ToolTakeSeat);
CLAUDE_TOOL(ToolReleaseSeat);
CLAUDE_TOOL(ToolSkipHumanTurn);
CLAUDE_TOOL(ToolTestingMode);

// ClaudeBriefing.cpp
CLAUDE_TOOL(ToolBriefing);
CLAUDE_TOOL(ToolCivs);
CLAUDE_TOOL(ToolLookup);

// ClaudeActions.cpp
CLAUDE_TOOL(ToolPolicyOptions);
CLAUDE_TOOL(ToolAdoptPolicy);
CLAUDE_TOOL(ToolPromoteUnit);
CLAUDE_TOOL(ToolUpgradeUnit);
CLAUDE_TOOL(ToolAttack);
CLAUDE_TOOL(ToolCityAttack);
CLAUDE_TOOL(ToolBuy);
CLAUDE_TOOL(ToolBuyTile);
CLAUDE_TOOL(ToolSetCityFocus);
CLAUDE_TOOL(ToolCityCitizens);
CLAUDE_TOOL(ToolCityWorkTile);
CLAUDE_TOOL(ToolCitySpecialist);
CLAUDE_TOOL(ToolDeclareWar);
CLAUDE_TOOL(ToolCityTask);

// ClaudeDiplomacy.cpp
CLAUDE_TOOL(ToolDiplomacy);
CLAUDE_TOOL(ToolRespondToRequest);
CLAUDE_TOOL(ToolProposeDeal);
CLAUDE_TOOL(ToolMakePeace);
CLAUDE_TOOL(ToolDemand);
CLAUDE_TOOL(ToolAnswerDeal);
CLAUDE_TOOL(ToolDiscuss);

// ClaudeReligion.cpp
CLAUDE_TOOL(ToolReligionOptions);
CLAUDE_TOOL(ToolFoundPantheon);
CLAUDE_TOOL(ToolFoundReligion);
CLAUDE_TOOL(ToolEnhanceReligion);
CLAUDE_TOOL(ToolAddReformationBelief);
CLAUDE_TOOL(ToolChooseGreatPerson);

// ClaudeCongress.cpp
CLAUDE_TOOL(ToolCongress);
CLAUDE_TOOL(ToolProposeResolution);
CLAUDE_TOOL(ToolVoteResolution);
CLAUDE_TOOL(ToolAbstain);

// ClaudeEspionage.cpp
CLAUDE_TOOL(ToolSpies);
CLAUDE_TOOL(ToolMoveSpy);
CLAUDE_TOOL(ToolStageCoup);
CLAUDE_TOOL(ToolStealTech);

// ClaudeMinorCivs.cpp
CLAUDE_TOOL(ToolCityState);
CLAUDE_TOOL(ToolCityStateAction);

// ClaudeCulture.cpp
CLAUDE_TOOL(ToolChooseIdeology);
CLAUDE_TOOL(ToolSwitchIdeology);
CLAUDE_TOOL(ToolArchaeology);
CLAUDE_TOOL(ToolGreatWorks);
CLAUDE_TOOL(ToolMoveGreatWork);
CLAUDE_TOOL(ToolTradeRouteOptions);
CLAUDE_TOOL(ToolEstablishTradeRoute);
CLAUDE_TOOL(ToolLaunchSpaceship);
