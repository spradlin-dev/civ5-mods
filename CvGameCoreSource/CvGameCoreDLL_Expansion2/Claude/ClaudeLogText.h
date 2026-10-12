#pragma once

#include <string>
#include "picojson.h"

struct ClaudeToolDef;

// Categories the Claude Log window filters on; mod/UI/ClaudeLog.lua lists the same names.
#define CLAUDE_LOG_ALWAYS "always"
#define CLAUDE_LOG_REASONING "reasoning"
#define CLAUDE_LOG_TURN "turn"
#define CLAUDE_LOG_MOVEMENT "movement" // military units moving
#define CLAUDE_LOG_MILITARY "military" // military units' other orders
#define CLAUDE_LOG_WORKERS "workers"
#define CLAUDE_LOG_SETTLERS "settlers"
#define CLAUDE_LOG_CIVILIAN "civilian" // great people, caravans, missionaries and other civilians
#define CLAUDE_LOG_COMBAT "combat"
#define CLAUDE_LOG_CITY "city"
#define CLAUDE_LOG_EMPIRE "empire"
#define CLAUDE_LOG_DIPLOMACY "diplomacy"
#define CLAUDE_LOG_MESSAGES "messages"
#define CLAUDE_LOG_NOTIFICATIONS "notifications"

// Game thread. How an action call reads in the log, before it runs.
const char* ClaudeLogCategory(const ClaudeToolDef& tool, const picojson::object& args);
std::string ClaudeDescribeCall(const ClaudeToolDef& tool, const picojson::object& args);
// A line about the outcome, or "" when the call itself says enough.
std::string ClaudeDescribeResult(const ClaudeToolDef& tool, const picojson::value& result);
