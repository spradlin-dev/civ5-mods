#pragma once

#include <string>
#include "picojson.h"

class CvDeal;

// Diplomacy for Claude's seat without the leader screens. Game thread only.

// Called by the game where it would open a leader screen for the player on
// screen. Returns true when the request was recorded for Claude's off-screen
// seat instead, so the game must not queue or show it.
bool ClaudeInterceptRequest(PlayerTypes eFrom, PlayerTypes eTo, DiploUIStateTypes eState, const char* szMessage, int iExtraGameData);
bool ClaudeInterceptDealRequest(PlayerTypes eFrom, PlayerTypes eTo, const CvDeal* pDeal, DiploUIStateTypes eState, const char* szMessage);

// Requests still waiting for Claude's answer.
picojson::value ClaudeDiploRequestsJson();
int ClaudeCountDiploRequests();
void ClaudeResetDiploRequests();
