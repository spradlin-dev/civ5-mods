#pragma once

#include <string>

// Serves MCP (JSON-RPC over HTTP) to Claude. Requests arrive on the HTTP thread;
// anything that reads or changes game state is queued and run by Tick() on the
// game thread, so game code never runs off its own thread.
class ClaudeBridge
{
public:
	static ClaudeBridge& Get();

	// Game thread, once per frame.
	void Tick();

	// Game thread. Appends to the in-game log, inserting a separator when the turn changed.
	void LogLine(const std::string& text);

	// Any thread. out receives the entries joined with '\n'; the returned sequence
	// number grows with every line ever appended, so callers can detect new lines
	// even after old ones were trimmed.
	int GetLogText(std::string& out);

private:
	ClaudeBridge();
	~ClaudeBridge();

	struct Impl;
	friend struct Impl;
	Impl* m_pImpl;
};
