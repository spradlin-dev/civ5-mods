#pragma once

#include <string>
#include "picojson.h"

// A tool exposed over MCP. The handler runs on the game thread; it fills error
// (and returns any value) to report a failure to Claude.
#define CLAUDE_TOOL_ADVANCE_ROUND "advance_round"
#define CLAUDE_TOOL_END_TURN "end_turn"
#define CLAUDE_TOOL_SKIP_HUMAN_TURN "skip_human_turn"
#define CLAUDE_TOOL_STATUS "status"

enum ClaudeLogKind
{
	LOG_NONE,
	LOG_MOVE,
	LOG_UNIT, // military or civilian, by the unit_id argument
	LOG_COMBAT,
	LOG_CITY,
	LOG_EMPIRE,
	LOG_DIPLOMACY,
	LOG_MESSAGES,
	LOG_TURN
};

struct ClaudeToolDef
{
	const char* name;
	const char* description;
	const char* inputSchema; // JSON text
	picojson::value (*handler)(const picojson::object& args, std::string& error); // NULL: the bridge runs it on the HTTP thread (see CallTool)
	ClaudeLogKind logAs; // LOG_NONE for queries; actions are echoed into the in-game log under this kind
	bool waitsForTurn;  // the HTTP thread blocks until it is Claude's turn before running the handler
};

const ClaudeToolDef* ClaudeGetTools(size_t& count);
const ClaudeToolDef* ClaudeFindTool(const std::string& name);
// "" when args match the tool's schema; otherwise the first unknown or missing argument.
std::string ClaudeCheckArgs(const ClaudeToolDef& tool, const picojson::object& args);
