#include "CvGameCoreDLLPCH.h"
#include <deque>
#include <algorithm>
#include "ClaudeBridge.h"
#include "ClaudeCrash.h"
#include "ClaudeHttp.h"
#include "ClaudeTools.h"
#include "ClaudeToolSupport.h"
#include "ClaudePopups.h"
#include "ClaudeSeat.h"
#include "ClaudeDiplomacy.h"
#include "ClaudeLogText.h"
#include "../CvGame.h"
#include "../CvPlayer.h"
#include "../CvGlobals.h"

#define CLAUDE_PORT 7676
#define CLAUDE_JOB_TIMEOUT_MS 15000
#define CLAUDE_START_RETRY_MS 10000
#define CLAUDE_GAME_IDLE_MS 3000
#define CLAUDE_WAIT_DEFAULT_SECONDS 25.0
#define CLAUDE_WAIT_MAX_SECONDS 55.0
#define CLAUDE_MAX_LOG_ENTRIES 2000
#define CLAUDE_MAX_POPUPS 200

struct ClaudePendingPopup
{
	int type, data1, data2, data3;
	bool option1;
	std::string text;
	bool onScreen;
};

struct ClaudeJob
{
	std::string tool;
	picojson::object args;
	picojson::value result;
	std::string error;
	bool done;
	bool abandoned;
	bool retrying;
	DWORD firstTryMs;
	HANDLE event;

	ClaudeJob() : done(false), abandoned(false), retrying(false), firstTryMs(0), event(CreateEvent(NULL, TRUE, FALSE, NULL)) {}
	~ClaudeJob() { CloseHandle(event); }
};

namespace
{
	void DiagLog(const std::string& msg)
	{
		FILogFile* pLog = LOGFILEMGR.GetLog("MCPServer.log", FILogFile::kDontTimeStamp);
		if(pLog) pLog->Msg("%s", msg.c_str());
	}

	picojson::value JsonRpcResponse(const picojson::value& id, const picojson::value& result)
	{
		picojson::object o;
		o["jsonrpc"] = picojson::value("2.0");
		o["id"] = id;
		o["result"] = result;
		return picojson::value(o);
	}

	picojson::value JsonRpcError(const picojson::value& id, int code, const std::string& message)
	{
		picojson::object err;
		err["code"] = picojson::value((double)code);
		err["message"] = picojson::value(message);
		picojson::object o;
		o["jsonrpc"] = picojson::value("2.0");
		o["id"] = id;
		o["error"] = picojson::value(err);
		return picojson::value(o);
	}

	// Guards against DNS rebinding: a browser page may only reach the server if it
	// was itself served from this machine.
	bool OriginAllowed(const ClaudeHttpRequest& req)
	{
		std::map<std::string, std::string>::const_iterator it = req.headers.find("origin");
		if(it == req.headers.end()) return true;
		const std::string& origin = it->second;
		size_t schemeEnd = origin.find("://");
		if(schemeEnd == std::string::npos) return false;
		std::string host = origin.substr(schemeEnd + 3);
		size_t hostEnd = host.find_first_of(":/");
		if(hostEnd != std::string::npos) host = host.substr(0, hostEnd);
		for(size_t i = 0; i < host.size(); ++i) host[i] = (char)tolower((unsigned char)host[i]);
		return host == "localhost" || host == "127.0.0.1";
	}
}

struct ClaudeBridge::Impl
{
	ClaudeHttpServer* pServer;
	DWORD lastStartAttemptMs;
	bool startFailureShown;
	CRITICAL_SECTION lock; // guards jobs, log and every job state transition
	std::deque<ClaudeJob*> jobs;
	std::deque<ClaudeLogEntry> log;
	ClaudeIdentity identity;
	std::vector<ClaudeModInfo> activeMods;
	bool gameBriefingDue;
	volatile int identityVersion; // bumped on every change; Tick relabels the seat when it moves
	int labelledIdentityVersion;  // game thread
	int logSeq;
	int lastLogTurn;
	std::vector<ClaudePendingPopup> pendingPopups; // written by the UI thread, drained by Tick
	int pendingDropped;
	std::vector<ClaudePopup> popups;
	int popupSeq;
	std::string dialogsToClose; // newline-joined; ClaudeLog.lua looks each name up under /InGame/
	unsigned int gameSeed;

	// Written by the game thread, read by the HTTP thread.
	volatile int gameTurn;
	volatile bool testingMode;
	volatile bool shuttingDown;
	volatile bool myTurn;
	volatile bool screenHumanTurn; // the human on screen (not Claude's seat) may act
	volatile bool logVisible;
	volatile DWORD lastTickMs;

	Impl();
	~Impl();

	// game thread
	void Tick();
	void TryStart();
	void Shutdown();
	void UpdateTurnFlags();
	bool RunJob(ClaudeJob* pJob); // true: run it again on a later frame
	void LogLine(const char* category, const std::string& text);
	void DrainPopups();
	void ResetForNewGame();

	// any thread
	int GetLog(std::vector<ClaudeLogEntry>& out);
	bool GameRunning() const { return lastTickMs != 0 && GetTickCount() - lastTickMs < CLAUDE_GAME_IDLE_MS; }

	// HTTP thread
	static void HttpHandler(const ClaudeHttpRequest& req, ClaudeHttpResponse& res);
	void HandleHttp(const ClaudeHttpRequest& req, ClaudeHttpResponse& res);
	picojson::value HandleRpc(const picojson::value& request, bool& bIsNotification);
	picojson::value CallTool(const std::string& name, const picojson::object& args, bool& bIsError);
	picojson::value AdvanceRound(const picojson::object& args, bool& bIsError);
	picojson::value RunOnGameThread(const std::string& tool, const picojson::object& args, std::string& error);
};

ClaudeBridge& ClaudeBridge::Get()
{
	static ClaudeBridge instance;
	return instance;
}

ClaudeBridge::ClaudeBridge() : m_pImpl(new Impl()) {}
ClaudeBridge::~ClaudeBridge() { delete m_pImpl; }
void ClaudeBridge::Tick() { m_pImpl->Tick(); }
void ClaudeBridge::Shutdown() { m_pImpl->Shutdown(); }
void ClaudeBridge::LogLine(const char* category, const std::string& text) { m_pImpl->LogLine(category, text); }

bool ClaudeBridge::LogVisibleOnScreen() const { return m_pImpl->logVisible; }

ClaudeIdentity ClaudeBridge::Identity()
{
	EnterCriticalSection(&m_pImpl->lock);
	ClaudeIdentity id = m_pImpl->identity;
	LeaveCriticalSection(&m_pImpl->lock);
	return id;
}

std::vector<ClaudeModInfo> ClaudeBridge::ActiveMods()
{
	EnterCriticalSection(&m_pImpl->lock);
	std::vector<ClaudeModInfo> mods = m_pImpl->activeMods;
	LeaveCriticalSection(&m_pImpl->lock);
	return mods;
}

void ClaudeBridge::SetActiveMods(const std::vector<ClaudeModInfo>& mods)
{
	EnterCriticalSection(&m_pImpl->lock);
	m_pImpl->activeMods = mods;
	m_pImpl->gameBriefingDue = true; // the UI reports mods once per loaded game
	LeaveCriticalSection(&m_pImpl->lock);
}

bool ClaudeBridge::TakeGameBriefingDue()
{
	EnterCriticalSection(&m_pImpl->lock);
	bool bDue = m_pImpl->gameBriefingDue;
	m_pImpl->gameBriefingDue = false;
	LeaveCriticalSection(&m_pImpl->lock);
	return bDue;
}

void ClaudeBridge::MarkGameBriefingDue()
{
	EnterCriticalSection(&m_pImpl->lock);
	m_pImpl->gameBriefingDue = true;
	LeaveCriticalSection(&m_pImpl->lock);
}

void ClaudeBridge::SetIdentity(const ClaudeIdentity& identity)
{
	EnterCriticalSection(&m_pImpl->lock);
	m_pImpl->identity = identity;
	++m_pImpl->identityVersion;
	LeaveCriticalSection(&m_pImpl->lock);
}
int ClaudeBridge::GetLog(std::vector<ClaudeLogEntry>& out) { return m_pImpl->GetLog(out); }

void ClaudeBridge::NotePopup(int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text, bool bOnScreen)
{
	ClaudePendingPopup p;
	p.type = iType; p.data1 = iData1; p.data2 = iData2; p.data3 = iData3; p.option1 = bOption1; p.text = text; p.onScreen = bOnScreen;
	EnterCriticalSection(&m_pImpl->lock);
	if(m_pImpl->pendingPopups.size() < CLAUDE_MAX_POPUPS) m_pImpl->pendingPopups.push_back(p);
	else ++m_pImpl->pendingDropped;
	LeaveCriticalSection(&m_pImpl->lock);
}

std::string ClaudeBridge::TakeDialogsToClose()
{
	std::string out;
	EnterCriticalSection(&m_pImpl->lock);
	out.swap(m_pImpl->dialogsToClose);
	LeaveCriticalSection(&m_pImpl->lock);
	return out;
}

void ClaudeBridge::SetTestingMode(bool bOn) { m_pImpl->testingMode = bOn; }
bool ClaudeBridge::TestingMode() const { return m_pImpl->testingMode; }

int ClaudeBridge::CountUnread()
{
	int n = 0;
	EnterCriticalSection(&m_pImpl->lock);
	for(size_t i = 0; i < m_pImpl->popups.size(); ++i)
		if(!m_pImpl->popups[i].read) ++n;
	LeaveCriticalSection(&m_pImpl->lock);
	return n;
}

int ClaudeBridge::Acknowledge(const std::vector<int>* ids)
{
	int n = 0;
	EnterCriticalSection(&m_pImpl->lock);
	for(size_t i = 0; i < m_pImpl->popups.size(); ++i)
	{
		ClaudePopup& p = m_pImpl->popups[i];
		if(p.read) continue;
		bool bWanted = (ids == NULL);
		for(size_t k = 0; !bWanted && k < ids->size(); ++k) bWanted = ((*ids)[k] == p.id);
		if(!bWanted) continue;
		p.read = true;
		++n;
		if(!p.dialog.empty()) m_pImpl->dialogsToClose += p.dialog + "\n";
	}
	LeaveCriticalSection(&m_pImpl->lock);
	return n;
}

std::vector<ClaudePopup> ClaudeBridge::GetPopupsSince(int iTurn)
{
	std::vector<ClaudePopup> out;
	EnterCriticalSection(&m_pImpl->lock);
	for(size_t i = 0; i < m_pImpl->popups.size(); ++i)
		if(m_pImpl->popups[i].turn >= iTurn || !m_pImpl->popups[i].read) out.push_back(m_pImpl->popups[i]);
	LeaveCriticalSection(&m_pImpl->lock);
	return out;
}

ClaudeBridge::Impl::Impl()
	: pServer(NULL), lastStartAttemptMs(0), startFailureShown(false), logSeq(0), gameBriefingDue(true), logVisible(true), identityVersion(0), labelledIdentityVersion(-1), lastLogTurn(-1), pendingDropped(0), popupSeq(0), gameSeed(0), gameTurn(-1), testingMode(false),
	  shuttingDown(false), myTurn(false), screenHumanTurn(false), lastTickMs(0)
{
	ClaudeInstallCrashHandler();
	InitializeCriticalSection(&lock);
}

ClaudeBridge::Impl::~Impl()
{
	Shutdown(); // last resort if the DLL's Uninit never ran
	DeleteCriticalSection(&lock);
}

// Static destruction runs under the loader lock while the DLL unloads; the HTTP
// thread can never finish exiting then, the join times out and the thread is
// left running in unmapped code. So the game thread calls this first.
void ClaudeBridge::Impl::Shutdown()
{
	shuttingDown = true;
	ClaudeUninstallCrashHandler();
	if(pServer)
	{
		EnterCriticalSection(&lock);
		for(size_t i = 0; i < jobs.size(); ++i)
		{
			ClaudeJob* pJob = jobs[i];
			pJob->error = "the game is shutting down";
			pJob->done = true;
			SetEvent(pJob->event);
		}
		jobs.clear();
		LeaveCriticalSection(&lock);

		pServer->Stop(CLAUDE_JOB_TIMEOUT_MS + 5000);
		delete pServer;
		pServer = NULL;
	}
}

void ClaudeBridge::Impl::TryStart()
{
	DWORD now = GetTickCount();
	if(lastStartAttemptMs != 0 && now - lastStartAttemptMs < CLAUDE_START_RETRY_MS) return;
	lastStartAttemptMs = now;

	shuttingDown = false;
	pServer = new ClaudeHttpServer();
	if(pServer->Start(CLAUDE_PORT, &Impl::HttpHandler))
	{
		DiagLog(ClaudeFormat("Claude bridge listening on http://127.0.0.1:%d/mcp", CLAUDE_PORT));
		return;
	}
	std::string msg = "Claude bridge FAILED to start: " + pServer->lastError();
	DiagLog(msg);
	if(!startFailureShown)
	{
		startFailureShown = true;
		LogLine(CLAUDE_LOG_ALWAYS, msg + " (retrying)");
	}
	delete pServer;
	pServer = NULL;
}

void ClaudeBridge::Impl::UpdateTurnFlags()
{
	PlayerTypes eSeat = ClaudeSeat();
	myTurn = (eSeat != NO_PLAYER) && ClaudeIsPlayerTurn(GET_PLAYER(eSeat));
	PlayerTypes eActive = GC.getGame().getActivePlayer();
	screenHumanTurn = (eActive != NO_PLAYER) && eActive != eSeat && ClaudeIsPlayerTurn(GET_PLAYER(eActive));
}

void ClaudeBridge::Impl::Tick()
{
	DWORD now = GetTickCount();
	lastTickMs = (now == 0) ? 1 : now;
	if(pServer == NULL) TryStart();
	int iTurn = GC.getGame().getGameTurn();
	if(CvPreGame::mapRandomSeed() != gameSeed || iTurn < gameTurn) ResetForNewGame();
	gameTurn = iTurn;
	logVisible = ClaudeLogVisibleOnScreen();
	if(identityVersion != labelledIdentityVersion)
	{
		labelledIdentityVersion = identityVersion;
		ClaudeUpdateSeatLabel();
	}
	UpdateTurnFlags();
	DrainPopups();

	for(;;)
	{
		ClaudeJob* pJob = NULL;
		EnterCriticalSection(&lock);
		if(!jobs.empty())
		{
			pJob = jobs.front();
			jobs.pop_front();
		}
		LeaveCriticalSection(&lock);
		if(pJob == NULL) break;

		bool bRetry = RunJob(pJob);
		UpdateTurnFlags();
		if(bRetry)
		{
			// Jobs behind it wait for the retry window; one MCP client sends one call at a time.
			EnterCriticalSection(&lock);
			jobs.push_front(pJob);
			LeaveCriticalSection(&lock);
			break;
		}

		EnterCriticalSection(&lock);
		pJob->done = true;
		bool bAbandoned = pJob->abandoned;
		if(!bAbandoned) SetEvent(pJob->event);
		LeaveCriticalSection(&lock);
		if(bAbandoned) delete pJob;
	}
}

void ClaudeBridge::Impl::DrainPopups()
{
	std::vector<ClaudePendingPopup> pending;
	EnterCriticalSection(&lock);
	pending.swap(pendingPopups);
	int iLost = pendingDropped;
	pendingDropped = 0;
	LeaveCriticalSection(&lock);
	if(iLost > 0) LogLine(CLAUDE_LOG_MESSAGES, ClaudeFormat("[bridge] %d game message(s) were lost: more than %d arrived before the game thread read them", iLost, CLAUDE_MAX_POPUPS));
	bool bScreenIsMine = ClaudeSeatOnScreen();
	for(size_t i = 0; i < pending.size(); ++i)
	{
		const ClaudePendingPopup& p = pending[i];
		if(p.onScreen && !bScreenIsMine) // the human's own popup
		{
			const char* dialog = ClaudePopupDialogToClose(p.type);
			if(testingMode && *dialog)
			{
				EnterCriticalSection(&lock);
				dialogsToClose += std::string(dialog) + "\n";
				LeaveCriticalSection(&lock);
				LogLine(CLAUDE_LOG_TURN, std::string("Testing: closed the human's ") + dialog + " unread");
			}
			continue;
		}
		ClaudePopup popup;
		popup.turn = GC.getGame().getGameTurn();
		popup.dialog = p.onScreen ? ClaudePopupDialogToClose(p.type) : "";
		popup.text = ClaudeDescribePopup(p.type, p.data1, p.data2, p.data3, p.option1, p.text);
		if(popup.text.empty())
		{
			if(popup.dialog.empty()) continue;
			popup.text = "The game showed a " + popup.dialog + " dialog with no text";
		}
		popup.read = false;
		int iDroppedUnread = 0;
		EnterCriticalSection(&lock);
		popup.id = ++popupSeq;
		popups.push_back(popup);
		if(popups.size() > CLAUDE_MAX_POPUPS)
		{
			size_t excess = popups.size() - CLAUDE_MAX_POPUPS;
			for(size_t k = 0; k < excess; ++k)
			{
				if(popups[k].read) continue;
				++iDroppedUnread;
				if(!popups[k].dialog.empty()) dialogsToClose += popups[k].dialog + "\n";
			}
			popups.erase(popups.begin(), popups.begin() + excess);
		}
		LeaveCriticalSection(&lock);
		if(iDroppedUnread > 0) LogLine(CLAUDE_LOG_MESSAGES, ClaudeFormat("[bridge] %d unread game message(s) dropped: more than %d were kept", iDroppedUnread, CLAUDE_MAX_POPUPS));
		LogLine(CLAUDE_LOG_MESSAGES, ClaudeFormat("Game message #%d: ", popup.id) + popup.text);
	}
}

// Keeps pendingPopups: the UI may already have reported the new game's first popups.
void ClaudeBridge::Impl::ResetForNewGame()
{
	EnterCriticalSection(&lock);
	gameSeed = CvPreGame::mapRandomSeed();
	popups.clear();
	popupSeq = 0;
	dialogsToClose.clear();
	testingMode = false;
	LeaveCriticalSection(&lock);
	labelledIdentityVersion = -1; // a loaded save carries the label of whichever model played it
	ClaudeGameId();
	ClaudeResetDiploRequests();
}

namespace
{
	bool s_retryRequested = false;
	const DWORD CLAUDE_RETRY_WINDOW_MS = 4000;
}

void ClaudeBridge::RetryLater() { s_retryRequested = true; }

bool ClaudeBridge::Impl::RunJob(ClaudeJob* pJob)
{
	const ClaudeToolDef* pTool = ClaudeFindTool(pJob->tool);
	if(pTool == NULL)
	{
		pJob->error = "unknown tool: " + pJob->tool;
		return false;
	}

	if(pTool->handler == NULL)
	{
		pJob->error = pJob->tool + " runs on the bridge, not the game thread";
		return false;
	}
	bool bFirstTry = !pJob->retrying;
	bool bLogged = pTool->logAs != LOG_NONE;
	const char* szCategory = bLogged ? ClaudeLogCategory(*pTool, pJob->args) : "";
	if(bLogged && bFirstTry) LogLine(szCategory, ClaudeDescribeCall(*pTool, pJob->args));
	ClaudeCrashSetContext((pJob->tool + " " + picojson::value(pJob->args).serialize()).c_str());
	s_retryRequested = false;
	pJob->error.clear();
	pJob->result = pTool->handler(pJob->args, pJob->error);
	ClaudeCrashSetContext("");
	if(s_retryRequested)
	{
		DWORD now = GetTickCount();
		if(bFirstTry)
		{
			pJob->retrying = true;
			pJob->firstTryMs = now;
		}
		if(now - pJob->firstTryMs < CLAUDE_RETRY_WINDOW_MS) return true;
	}
	if(!bLogged) return false;
	std::string outcome = pJob->error.empty() ? ClaudeDescribeResult(*pTool, pJob->result) : "Refused: " + pJob->error;
	if(!outcome.empty()) LogLine(szCategory, "    " + outcome);
	return false;
}

void ClaudeBridge::Impl::LogLine(const char* category, const std::string& text)
{
	int iTurn = GC.getGame().getGameTurn();
	EnterCriticalSection(&lock);
	if(iTurn != lastLogTurn)
	{
		ClaudeLogEntry separator = { CLAUDE_LOG_ALWAYS, ClaudeFormat("---------- Turn %d ----------", iTurn) };
		log.push_back(separator);
		++logSeq;
		lastLogTurn = iTurn;
	}
	ClaudeLogEntry entry = { category, text };
	log.push_back(entry);
	++logSeq;
	while(log.size() > CLAUDE_MAX_LOG_ENTRIES) log.pop_front();
	LeaveCriticalSection(&lock);

	FILogFile* pLog = LOGFILEMGR.GetLog("AIThinking.log", FILogFile::kDontTimeStamp);
	if(pLog) pLog->Msg("[%s] %s", category, text.c_str());
}

int ClaudeBridge::Impl::GetLog(std::vector<ClaudeLogEntry>& out)
{
	EnterCriticalSection(&lock);
	out.assign(log.begin(), log.end());
	int seq = logSeq;
	LeaveCriticalSection(&lock);
	return seq;
}

// ---------------------------------------------------------------- HTTP thread

void ClaudeBridge::Impl::HttpHandler(const ClaudeHttpRequest& req, ClaudeHttpResponse& res)
{
	ClaudeBridge::Get().m_pImpl->HandleHttp(req, res);
}

void ClaudeBridge::Impl::HandleHttp(const ClaudeHttpRequest& req, ClaudeHttpResponse& res)
{
	if(!OriginAllowed(req))
	{
		res.status = 403;
		res.body = "{\"error\":\"origin not allowed\"}";
		return;
	}
	if(req.path != "/mcp")
	{
		res.status = 404;
		res.body = "{\"error\":\"the MCP endpoint is /mcp\"}";
		return;
	}
	if(req.method != "POST")
	{
		res.status = 405;
		res.body = "{\"error\":\"only POST is supported\"}";
		return;
	}

	picojson::value request;
	std::string parseError = picojson::parse(request, req.body);
	if(parseError.empty() && !request.is<picojson::object>())
		parseError = "the body must be a single JSON-RPC object";
	if(!parseError.empty())
	{
		res.status = 400;
		res.body = JsonRpcError(picojson::value(), -32700, "parse error: " + parseError).serialize();
		return;
	}

	bool bIsNotification = false;
	picojson::value response = HandleRpc(request, bIsNotification);
	if(bIsNotification)
	{
		res.status = 202;
		res.body.clear();
		return;
	}
	res.status = 200;
	res.body = response.serialize();
}

picojson::value ClaudeBridge::Impl::HandleRpc(const picojson::value& request, bool& bIsNotification)
{
	const picojson::object& o = request.get<picojson::object>();
	picojson::object::const_iterator itMethod = o.find("method");
	picojson::object::const_iterator itId = o.find("id");
	picojson::object::const_iterator itParams = o.find("params");
	std::string method = (itMethod != o.end() && itMethod->second.is<std::string>()) ? itMethod->second.get<std::string>() : "";
	picojson::value id = (itId != o.end()) ? itId->second : picojson::value();
	picojson::object params;
	if(itParams != o.end() && itParams->second.is<picojson::object>())
		params = itParams->second.get<picojson::object>();

	if(method.find("notifications/") == 0 || itId == o.end())
	{
		bIsNotification = true;
		return picojson::value();
	}

	if(method == "initialize")
	{
		ClaudeBridge::Get().SetIdentity(ClaudeIdentity());
		ClaudeBridge::Get().MarkGameBriefingDue();
		std::string version = "2025-06-18";
		picojson::object::const_iterator itV = params.find("protocolVersion");
		if(itV != params.end() && itV->second.is<std::string>() && !itV->second.get<std::string>().empty())
			version = itV->second.get<std::string>();

		picojson::object caps;
		caps["tools"] = picojson::value(picojson::object());
		picojson::object info;
		info["name"] = picojson::value("civ5-local-mcp-server");
		info["version"] = picojson::value("0.1");
		picojson::object result;
		result["protocolVersion"] = picojson::value(version);
		result["capabilities"] = picojson::value(caps);
		result["serverInfo"] = picojson::value(info);
		result["instructions"] = picojson::value(
		    "You are playing Civilization V as the active human player. First, name yourself with set_identity: "
		    "your provider (Claude, ChatGPT, Gemini...), model (Opus, Fable, GPT...) and version (5.5...); actions are refused until you do, "
		    "and you must do it again after every reconnect. Then call wait_for_my_turn until it is your turn, "
		    "use the query tools, act, write your reasoning with log, and finish with end_turn and a short summary of what you did. "
		    "A unit with a queued multi-turn move is still where it stopped when your next turn opens; it moves when you call end_turn, "
		    "as for a human clicking End Turn. Leave it alone unless you want it to go somewhere else. "
		    "status.game_id names this game across reloads and reconnections: keep your memories about this game "
		    "(your role, plans, what you learned about the other players) under that id, keep lessons for every game separate, "
		    "and when you connect, look up your memories for this game_id before you play. "
		    "The first status after you connect, or after another game is loaded, also carries game_settings (era, map, difficulty, "
		    "victory types and the game options that change the rules, such as no happiness) and the loaded mods with what each does. "
		    "Store both with your memories for this game_id; mods can change between loads, so compare them with what you remembered. "
		    "Later statuses list only the mod names; the game_settings and mods tools show the full detail again. "
		    "Right after naming yourself, call status and tell the human in your reply who you are and what you play: "
		    "their own civilization, a teammate (name it), or an opponent (never name it; they will see you once they meet you). "
		    "If no seat is claimed and the human has not told you your role, ask them first and offer the choices: "
		    "play their civilization, a teammate (status.teammate_civs_you_could_play), or a secret opponent. "
		    "Claim a teammate or opponent with take_seat during their turn; civ=opponent lets the game pick one at random. "
		    "Until a seat is claimed every tool acts on the human's own civilization. A claimed seat is off screen; "
		    "your turn comes after the human's each round, so wait_for_my_turn may take as long as they do. "
		    "Other leaders talk to you through diplomacy requests (briefing.diplomacy_requests): answer them with respond_to_request. "
		    "Use propose_deal, demand, discuss and make_peace for your own diplomacy.");
		return JsonRpcResponse(id, picojson::value(result));
	}

	if(method == "ping")
	{
		return JsonRpcResponse(id, picojson::value(picojson::object()));
	}

	if(method == "tools/list")
	{
		size_t count = 0;
		const ClaudeToolDef* tools = ClaudeGetTools(count);
		picojson::array list;
		for(size_t i = 0; i < count; ++i)
		{
			picojson::value schema;
			std::string err = picojson::parse(schema, std::string(tools[i].inputSchema));
			picojson::object t;
			t["name"] = picojson::value(tools[i].name);
			t["description"] = picojson::value(tools[i].description);
			t["inputSchema"] = schema;
			if(!err.empty())
			{
				DiagLog(std::string("tool schema for ") + tools[i].name + " does not parse: " + err);
				t["description"] = picojson::value("BROKEN TOOL SCHEMA (" + err + "): " + tools[i].description);
			}
			list.push_back(picojson::value(t));
		}
		picojson::object result;
		result["tools"] = picojson::value(list);
		return JsonRpcResponse(id, picojson::value(result));
	}

	if(method == "tools/call")
	{
		picojson::object::const_iterator itName = params.find("name");
		if(itName == params.end() || !itName->second.is<std::string>())
			return JsonRpcError(id, -32602, "tools/call needs a name");
		picojson::object args;
		picojson::object::const_iterator itArgs = params.find("arguments");
		if(itArgs != params.end() && itArgs->second.is<picojson::object>())
			args = itArgs->second.get<picojson::object>();

		bool bIsError = false;
		picojson::value value = CallTool(itName->second.get<std::string>(), args, bIsError);

		picojson::object text;
		text["type"] = picojson::value("text");
		text["text"] = picojson::value(value.is<std::string>() ? value.get<std::string>() : value.serialize());
		picojson::array content;
		content.push_back(picojson::value(text));
		picojson::object result;
		result["content"] = picojson::value(content);
		result["isError"] = picojson::value(bIsError);
		return JsonRpcResponse(id, picojson::value(result));
	}

	return JsonRpcError(id, -32601, "method not found: " + method);
}

picojson::value ClaudeBridge::Impl::CallTool(const std::string& name, const picojson::object& args, bool& bIsError)
{
	const ClaudeToolDef* pTool = ClaudeFindTool(name);
	if(pTool == NULL)
	{
		bIsError = true;
		return picojson::value("unknown tool: " + name);
	}
	std::string argError = ClaudeCheckArgs(*pTool, args);
	if(!argError.empty())
	{
		bIsError = true;
		return picojson::value(argError);
	}

	if(pTool->handler == NULL)
	{
		if(name == CLAUDE_TOOL_ADVANCE_ROUND) return AdvanceRound(args, bIsError);
		bIsError = true;
		return picojson::value("no bridge handler for " + name);
	}

	if(pTool->waitsForTurn)
	{
		double timeout = CLAUDE_WAIT_DEFAULT_SECONDS;
		picojson::object::const_iterator it = args.find("timeout_seconds");
		if(it != args.end() && it->second.is<double>()) timeout = it->second.get<double>();
		if(timeout < 1.0) timeout = 1.0;
		if(timeout > CLAUDE_WAIT_MAX_SECONDS) timeout = CLAUDE_WAIT_MAX_SECONDS;

		DWORD start = GetTickCount();
		while(!shuttingDown && !(GameRunning() && myTurn) && GetTickCount() - start < (DWORD)(timeout * 1000.0))
			Sleep(250);
	}

	std::string error;
	picojson::value result = RunOnGameThread(name, args, error);
	if(!error.empty())
	{
		bIsError = true;
		return picojson::value(error);
	}
	return result;
}

// Solo testing: one call per round instead of end_turn, wait, skip_human_turn, wait.
picojson::value ClaudeBridge::Impl::AdvanceRound(const picojson::object& args, bool& bIsError)
{
	if(!testingMode) { bIsError = true; return picojson::value("advance_round is a testing tool: call testing_mode with enabled=true first"); }
	std::string error;
	int iTurnBefore = gameTurn;
	picojson::value r = RunOnGameThread(CLAUDE_TOOL_END_TURN, args, error);
	if(!error.empty()) { bIsError = true; return picojson::value(error); }
	DWORD start = GetTickCount();
	const DWORD limit = (DWORD)(CLAUDE_WAIT_MAX_SECONDS * 1000.0);
	while(!shuttingDown && myTurn && GetTickCount() - start < 5000) Sleep(100);
	bool bSkipped = false;
	while(!shuttingDown && GetTickCount() - start < limit)
	{
		if(GameRunning() && myTurn) break; // the turn came back (a unit arrived with moves left) or the round is done
		if(!bSkipped && screenHumanTurn)
		{
			picojson::object none;
			RunOnGameThread(CLAUDE_TOOL_SKIP_HUMAN_TURN, none, error);
			if(!error.empty()) { bIsError = true; return picojson::value(error); }
			bSkipped = true;
		}
		Sleep(250);
	}
	picojson::object none;
	r = RunOnGameThread(CLAUDE_TOOL_STATUS, none, error);
	if(!error.empty()) { bIsError = true; return picojson::value(error); }
	if(r.is<picojson::object>())
	{
		picojson::object& o = r.get<picojson::object>();
		o["round_advanced"] = picojson::value(gameTurn > iTurnBefore);
		o["turn_reopened"] = picojson::value(gameTurn == iTurnBefore && myTurn);
	}
	return r;
}

picojson::value ClaudeBridge::Impl::RunOnGameThread(const std::string& tool, const picojson::object& args, std::string& error)
{
	if(shuttingDown)
	{
		error = "the game is shutting down";
		return picojson::value();
	}
	if(!GameRunning())
	{
		error = "no game is running: the game core has not ticked recently (main menu, loading screen, or a modal dialog)";
		return picojson::value();
	}

	ClaudeJob* pJob = new ClaudeJob();
	pJob->tool = tool;
	pJob->args = args;

	EnterCriticalSection(&lock);
	bool bQueued = !shuttingDown;
	if(bQueued) jobs.push_back(pJob);
	LeaveCriticalSection(&lock);
	if(!bQueued)
	{
		delete pJob;
		error = "the game is shutting down";
		return picojson::value();
	}

	WaitForSingleObject(pJob->event, CLAUDE_JOB_TIMEOUT_MS);

	picojson::value result;
	bool bDeleteHere = false;
	EnterCriticalSection(&lock);
	if(pJob->done)
	{
		result = pJob->result;
		error = pJob->error;
		bDeleteHere = true;
	}
	else
	{
		std::deque<ClaudeJob*>::iterator it = std::find(jobs.begin(), jobs.end(), pJob);
		if(it != jobs.end())
		{
			jobs.erase(it);
			bDeleteHere = true;
		}
		else
		{
			pJob->abandoned = true; // running on the game thread, which will delete it
		}
		error = ClaudeFormat("the game did not respond within %d seconds; it may be busy with a dialog or loading", CLAUDE_JOB_TIMEOUT_MS / 1000);
	}
	LeaveCriticalSection(&lock);

	if(bDeleteHere) delete pJob;
	return result;
}
