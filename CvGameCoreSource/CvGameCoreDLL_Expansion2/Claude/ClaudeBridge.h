#pragma once

#include <string>
#include <vector>

// A game popup Claude must read. Modal for Claude the way it is for a human:
// action tools are refused until it is acknowledged, and the game dialog is
// only closed once it has been.
struct ClaudePopup
{
	int id;
	int turn;
	std::string text;
	std::string dialog; // InGame dialog context to close on acknowledgement; "" if a human must close it
	bool read;
};

// Serves MCP (JSON-RPC over HTTP) to Claude. Requests arrive on the HTTP thread;
// anything that reads or changes game state is queued and run by Tick() on the
// game thread, so game code never runs off its own thread.
class ClaudeBridge
{
public:
	static ClaudeBridge& Get();

	// Game thread, once per frame.
	void Tick();

	// Game thread. Stops serving and joins the HTTP thread. Called from the DLL's
	// own teardown; static destruction is too late (see the implementation).
	void Shutdown();

	// Game thread. Appends to the in-game log, inserting a separator when the turn changed.
	void LogLine(const std::string& text);

	// Any thread. out receives the entries joined with '\n'; the returned sequence
	// number grows with every line ever appended, so callers can detect new lines
	// even after old ones were trimmed.
	int GetLogText(std::string& out);

	// Records a game popup; it is described and logged on the next Tick. bOnScreen
	// means the game's dialog is open: it is Claude's only when Claude's seat is the
	// player on screen, and is then closed once Claude has read it.
	// UI thread (Lua) for on-screen popups, game thread otherwise.
	void NotePopup(int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text, bool bOnScreen);

	// UI thread (Lua). Dialog names whose messages were acknowledged since the last
	// call, one per line; the Lua side closes them.
	std::string TakeDialogsToClose();

	// Testing only: enables the tools that drive the idle human's turn and closes
	// that human's popups and leader screens unread. Off by default, reset every new game.
	void SetTestingMode(bool bOn);
	bool TestingMode() const;

	// Game thread.
	std::vector<ClaudePopup> GetPopupsSince(int iTurn);
	int CountUnread();
	// ids NULL = all. Returns how many were newly marked read.
	int Acknowledge(const std::vector<int>* ids);

private:
	ClaudeBridge();
	~ClaudeBridge();

	struct Impl;
	friend struct Impl;
	Impl* m_pImpl;
};
