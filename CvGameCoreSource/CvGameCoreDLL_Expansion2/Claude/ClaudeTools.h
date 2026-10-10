#pragma once

#include <string>
#include "picojson.h"

// A tool exposed over MCP. The handler runs on the game thread; it fills error
// (and returns any value) to report a failure to Claude.
struct ClaudeToolDef
{
	const char* name;
	const char* description;
	const char* inputSchema; // JSON text
	picojson::value (*handler)(const picojson::object& args, std::string& error);
	bool isAction;      // changes game state; the bridge echoes the call into the in-game log
	bool waitsForTurn;  // the HTTP thread blocks until it is Claude's turn before running the handler
};

const ClaudeToolDef* ClaudeGetTools(size_t& count);
const ClaudeToolDef* ClaudeFindTool(const std::string& name);
// "" when args match the tool's schema; otherwise the first unknown or missing argument.
std::string ClaudeCheckArgs(const ClaudeToolDef& tool, const picojson::object& args);
