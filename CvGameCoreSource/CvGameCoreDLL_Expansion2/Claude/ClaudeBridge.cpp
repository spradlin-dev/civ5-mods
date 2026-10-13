#include "CvGameCoreDLLPCH.h"
#include <deque>
#include <map>
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
#define CLAUDE_MAX_POPUPS 200                     // kept per player
#define CLAUDE_SESSION_IDLE_MS (2 * 60 * 1000)    // no request for this long: the model has most likely gone
#define CLAUDE_SESSION_EXPIRE_MS (10 * 60 * 1000) // no request for this long: the session is dropped

struct ClaudePendingPopup
{
	int player; // -1: the player on screen when drained
	int type, data1, data2, data3;
	bool option1;
	std::string text;
	bool onScreen;
};

struct ClaudeJob
{
	std::string session;
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
	// All fields are read and written under lock.
	struct Session
	{
		ClaudeSession info;
		bool briefingDue;
		bool myTurn;
		int diploSeq; // grows when a new request reaches the session's civilization
		int diploCount;
		DWORD lastSeenMs;
		Session() : briefingDue(true), myTurn(false), diploSeq(0), diploCount(0), lastSeenMs(0) {}
		bool Idle(DWORD now) const { return now - lastSeenMs > CLAUDE_SESSION_IDLE_MS; }
	};

	ClaudeHttpServer* pServer;
	DWORD lastStartAttemptMs;
	bool startFailureShown;
	CRITICAL_SECTION lock; // guards jobs, log, sessions, popups and every job state transition
	std::deque<ClaudeJob*> jobs;
	std::deque<ClaudeLogEntry> log;
	std::map<std::string, Session> sessions;
	bool inJob;               // game thread: a job is running, for the session named by currentToken
	std::string currentToken;
	std::vector<ClaudeModInfo> activeMods;
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
	volatile bool screenHumanTurn; // the human on screen may act
	volatile int hiddenPlayers;    // live sessions playing an opponent of the player on screen
	volatile DWORD lastTickMs;

	Impl();
	~Impl();

	// game thread
	void Tick();
	void TryStart();
	void Shutdown();
	void UpdateSessions();
	bool RunJob(ClaudeJob* pJob); // true: run it again on a later frame
	void Append(int player, const std::string& who, const char* category, const std::string& text);
	void DrainPopups();
	void ResetForNewGame();
	Session* CurrentSession(); // under lock

	// any thread
	int GetLog(std::vector<ClaudeLogEntry>& out);
	bool GameRunning() const { return lastTickMs != 0 && GetTickCount() - lastTickMs < CLAUDE_GAME_IDLE_MS; }
	void Touch(const std::string& token); // creates the session when new
	// The session's flags; false when it no longer exists.
	bool Flags(const std::string& token, bool& bMyTurn, int& iDiploSeq, int& iSeat);

	// HTTP thread
	static void HttpHandler(const ClaudeHttpRequest& req, ClaudeHttpResponse& res);
	void HandleHttp(const ClaudeHttpRequest& req, ClaudeHttpResponse& res);
	picojson::value HandleRpc(const picojson::value& request, const std::string& token, bool& bIsNotification);
	picojson::value CallTool(const std::string& token, const std::string& name, const picojson::object& args, bool& bIsError);
	picojson::value AdvanceRound(const std::string& token, const picojson::object& args, bool& bIsError);
	picojson::value RunOnGameThread(const std::string& token, const std::string& tool, const picojson::object& args, std::string& error);
	bool SessionWakes(const std::string& token, int iDiploSeqAtStart);
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

// ---------------------------------------------------------------- sessions

ClaudeBridge::Impl::Session* ClaudeBridge::Impl::CurrentSession()
{
	if(!inJob) return NULL;
	std::map<std::string, Session>::iterator it = sessions.find(currentToken);
	return (it == sessions.end()) ? NULL : &it->second;
}

void ClaudeBridge::Impl::Touch(const std::string& token)
{
	EnterCriticalSection(&lock);
	Session& s = sessions[token];
	s.info.token = token;
	s.lastSeenMs = GetTickCount();
	LeaveCriticalSection(&lock);
}

bool ClaudeBridge::Impl::Flags(const std::string& token, bool& bMyTurn, int& iDiploSeq, int& iSeat)
{
	EnterCriticalSection(&lock);
	std::map<std::string, Session>::iterator it = sessions.find(token);
	bool bFound = it != sessions.end();
	if(bFound)
	{
		bMyTurn = it->second.myTurn;
		iDiploSeq = it->second.diploSeq;
		iSeat = it->second.info.seat;
	}
	LeaveCriticalSection(&lock);
	return bFound;
}

ClaudeSession ClaudeBridge::Current()
{
	ClaudeSession out;
	EnterCriticalSection(&m_pImpl->lock);
	Impl::Session* s = m_pImpl->CurrentSession();
	if(s) out = s->info;
	LeaveCriticalSection(&m_pImpl->lock);
	return out;
}

void ClaudeBridge::SetCurrentIdentity(const ClaudeIdentity& identity)
{
	EnterCriticalSection(&m_pImpl->lock);
	Impl::Session* s = m_pImpl->CurrentSession();
	if(s) s->info.identity = identity;
	LeaveCriticalSection(&m_pImpl->lock);
}

void ClaudeBridge::SetCurrentSeat(int seat)
{
	EnterCriticalSection(&m_pImpl->lock);
	Impl::Session* s = m_pImpl->CurrentSession();
	if(s) s->info.seat = seat;
	LeaveCriticalSection(&m_pImpl->lock);
}

std::vector<ClaudeSession> ClaudeBridge::Sessions()
{
	std::vector<ClaudeSession> out;
	DWORD now = GetTickCount();
	EnterCriticalSection(&m_pImpl->lock);
	for(std::map<std::string, Impl::Session>::iterator it = m_pImpl->sessions.begin(); it != m_pImpl->sessions.end(); ++it)
	{
		ClaudeSession s = it->second.info;
		s.idle = it->second.Idle(now);
		out.push_back(s);
	}
	LeaveCriticalSection(&m_pImpl->lock);
	return out;
}

// For the screen a named session wins over one that has not named itself yet.
bool ClaudeBridge::SessionForSeat(int seat, ClaudeSession& out)
{
	bool bFound = false;
	DWORD now = GetTickCount();
	EnterCriticalSection(&m_pImpl->lock);
	for(std::map<std::string, Impl::Session>::iterator it = m_pImpl->sessions.begin(); it != m_pImpl->sessions.end(); ++it)
	{
		if(it->second.info.seat != seat || it->second.Idle(now)) continue;
		if(bFound && it->second.info.identity.model.empty()) continue;
		out = it->second.info;
		bFound = true;
		if(!out.identity.model.empty()) break;
	}
	LeaveCriticalSection(&m_pImpl->lock);
	return bFound;
}

void ClaudeBridge::UnbindSeat(int seat)
{
	EnterCriticalSection(&m_pImpl->lock);
	for(std::map<std::string, Impl::Session>::iterator it = m_pImpl->sessions.begin(); it != m_pImpl->sessions.end(); ++it)
		if(it->second.info.seat == seat) it->second.info.seat = CLAUDE_SEAT_LOST;
	LeaveCriticalSection(&m_pImpl->lock);
}

bool ClaudeBridge::ScreenPlayed()
{
	ClaudeSession s;
	return SessionForSeat(CLAUDE_SEAT_SCREEN, s);
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
	LeaveCriticalSection(&m_pImpl->lock);
	MarkGameBriefingDue(); // the UI reports mods once per loaded game
}

bool ClaudeBridge::TakeGameBriefingDue()
{
	bool bDue = false;
	EnterCriticalSection(&m_pImpl->lock);
	Impl::Session* s = m_pImpl->CurrentSession();
	if(s)
	{
		bDue = s->briefingDue;
		s->briefingDue = false;
	}
	LeaveCriticalSection(&m_pImpl->lock);
	return bDue;
}

void ClaudeBridge::MarkGameBriefingDue()
{
	EnterCriticalSection(&m_pImpl->lock);
	for(std::map<std::string, Impl::Session>::iterator it = m_pImpl->sessions.begin(); it != m_pImpl->sessions.end(); ++it)
		it->second.briefingDue = true;
	LeaveCriticalSection(&m_pImpl->lock);
}

// ---------------------------------------------------------------- log and popups

void ClaudeBridge::LogLine(const char* category, const std::string& text)
{
	if(!m_pImpl->inJob) { m_pImpl->Append(-1, "", category, text); return; }
	ClaudeSession s = Current();
	m_pImpl->Append((int)ClaudePlayerOf(s), ClaudeIdentityLabel(s.identity), category, text);
}

void ClaudeBridge::LogLineFor(int player, const char* category, const std::string& text)
{
	m_pImpl->Append(player, player >= 0 ? ClaudeSeatLabel((PlayerTypes)player) : "", category, text);
}

int ClaudeBridge::GetLog(std::vector<ClaudeLogEntry>& out) { return m_pImpl->GetLog(out); }
int ClaudeBridge::HiddenPlayers() const { return m_pImpl->hiddenPlayers; }

void ClaudeBridge::NotePopup(int player, int iType, int iData1, int iData2, int iData3, bool bOption1, const std::string& text, bool bOnScreen)
{
	ClaudePendingPopup p;
	p.player = player; p.type = iType; p.data1 = iData1; p.data2 = iData2; p.data3 = iData3; p.option1 = bOption1; p.text = text; p.onScreen = bOnScreen;
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

int ClaudeBridge::CountUnread(int player)
{
	int n = 0;
	EnterCriticalSection(&m_pImpl->lock);
	for(size_t i = 0; i < m_pImpl->popups.size(); ++i)
		if(m_pImpl->popups[i].player == player && !m_pImpl->popups[i].read) ++n;
	LeaveCriticalSection(&m_pImpl->lock);
	return n;
}

int ClaudeBridge::Acknowledge(int player, const std::vector<int>* ids)
{
	int n = 0;
	EnterCriticalSection(&m_pImpl->lock);
	for(size_t i = 0; i < m_pImpl->popups.size(); ++i)
	{
		ClaudePopup& p = m_pImpl->popups[i];
		if(p.read || p.player != player) continue;
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

std::vector<ClaudePopup> ClaudeBridge::GetPopupsSince(int player, int iTurn)
{
	std::vector<ClaudePopup> out;
	EnterCriticalSection(&m_pImpl->lock);
	for(size_t i = 0; i < m_pImpl->popups.size(); ++i)
		if(m_pImpl->popups[i].player == player && (m_pImpl->popups[i].turn >= iTurn || !m_pImpl->popups[i].read)) out.push_back(m_pImpl->popups[i]);
	LeaveCriticalSection(&m_pImpl->lock);
	return out;
}

// ---------------------------------------------------------------- game thread

ClaudeBridge::Impl::Impl()
	: pServer(NULL), lastStartAttemptMs(0), startFailureShown(false), inJob(false), logSeq(0), lastLogTurn(-1), pendingDropped(0), popupSeq(0), gameSeed(0),
	  gameTurn(-1), testingMode(false), shuttingDown(false), screenHumanTurn(false), hiddenPlayers(0), lastTickMs(0)
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
		Append(-1, "", CLAUDE_LOG_ALWAYS, msg + " (retrying)");
	}
	delete pServer;
	pServer = NULL;
}

// Per session: drops it when its model has been silent for long; a new session in a
// game without seats plays the screen, as the one model always did; a seat whose slot
// is no longer a seat is lost; a lost seat comes back when a waiting seat carries the
// model's own name (a reload of the same game); then its turn and diplomacy flags.
void ClaudeBridge::Impl::UpdateSessions()
{
	PlayerTypes eActive = GC.getGame().getActivePlayer();
	screenHumanTurn = (eActive != NO_PLAYER) && GET_PLAYER(eActive).isHuman() && ClaudeIsPlayerTurn(GET_PLAYER(eActive));
	DWORD now = GetTickCount();
	bool bAnySeat = ClaudeAnySeat();
	std::vector<PlayerTypes> unbound = ClaudeUnboundSeats();
	int iHidden = 0;
	EnterCriticalSection(&lock);
	for(std::map<std::string, Session>::iterator it = sessions.begin(); it != sessions.end();)
	{
		Session& s = it->second;
		if(now - s.lastSeenMs > CLAUDE_SESSION_EXPIRE_MS && !(inJob && it->first == currentToken))
		{
			sessions.erase(it++);
			continue;
		}
		if(s.info.seat == CLAUDE_SEAT_UNDECIDED && !bAnySeat) s.info.seat = CLAUDE_SEAT_SCREEN;
		if(s.info.seat >= 0 && !ClaudeIsSeat((PlayerTypes)s.info.seat)) s.info.seat = CLAUDE_SEAT_LOST;
		if(s.info.seat == CLAUDE_SEAT_LOST)
		{
			std::string label = ClaudeIdentityLabel(s.info.identity);
			for(size_t i = 0; i < unbound.size(); ++i)
			{
				if(!ClaudeSeatLabelMatches(unbound[i], label)) continue;
				for(std::map<std::string, Session>::iterator other = sessions.begin(); other != sessions.end(); ++other)
					if(other->second.info.seat == unbound[i]) other->second.info.seat = CLAUDE_SEAT_LOST; // an idle session of the same model
				s.info.seat = unbound[i];
				unbound.erase(unbound.begin() + i);
				break;
			}
		}
		if(s.info.seat >= 0 && !s.Idle(now) && !ClaudeOnScreenTeam((PlayerTypes)s.info.seat)) ++iHidden;
		PlayerTypes ePlayer = ClaudePlayerOf(s.info);
		s.myTurn = ePlayer != NO_PLAYER && ClaudeIsPlayerTurn(GET_PLAYER(ePlayer));
		int iRequests = ePlayer != NO_PLAYER ? ClaudeCountDiploRequestsFor(ePlayer) : 0;
		if(iRequests > s.diploCount) ++s.diploSeq;
		s.diploCount = iRequests;
		++it;
	}
	hiddenPlayers = iHidden;
	LeaveCriticalSection(&lock);
}

void ClaudeBridge::Impl::Tick()
{
	DWORD now = GetTickCount();
	lastTickMs = (now == 0) ? 1 : now;
	if(pServer == NULL) TryStart();
	int iTurn = GC.getGame().getGameTurn();
	if(CvPreGame::mapRandomSeed() != gameSeed || iTurn < gameTurn) ResetForNewGame();
	gameTurn = iTurn;
	UpdateSessions();
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

		currentToken = pJob->session;
		inJob = true;
		bool bRetry = RunJob(pJob);
		inJob = false;
		UpdateSessions();
		if(bRetry)
		{
			// Jobs behind it wait for the retry window; each MCP client sends one call at a time.
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
	if(iLost > 0) Append(-1, "", CLAUDE_LOG_MESSAGES, ClaudeFormat("[bridge] %d game message(s) were lost: more than %d arrived before the game thread read them", iLost, CLAUDE_MAX_POPUPS));
	bool bScreenPlayed = ClaudeBridge::Get().ScreenPlayed();
	PlayerTypes eActive = GC.getGame().getActivePlayer();
	for(size_t i = 0; i < pending.size(); ++i)
	{
		const ClaudePendingPopup& p = pending[i];
		if(p.onScreen && !bScreenPlayed) // the human's own popup
		{
			const char* dialog = ClaudePopupDialogToClose(p.type);
			if(testingMode && *dialog)
			{
				EnterCriticalSection(&lock);
				dialogsToClose += std::string(dialog) + "\n";
				LeaveCriticalSection(&lock);
				Append(-1, "", CLAUDE_LOG_TURN, std::string("Testing: closed the human's ") + dialog + " unread");
			}
			continue;
		}
		ClaudePopup popup;
		popup.turn = GC.getGame().getGameTurn();
		popup.player = p.onScreen ? (int)eActive : p.player;
		popup.dialog = p.onScreen ? ClaudePopupDialogToClose(p.type) : "";
		popup.text = ClaudeDescribePopup((PlayerTypes)popup.player, p.type, p.data1, p.data2, p.data3, p.option1, p.text);
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
		int iKept = 0;
		for(size_t k = 0; k < popups.size(); ++k)
			if(popups[k].player == popup.player) ++iKept;
		for(size_t k = 0; iKept > CLAUDE_MAX_POPUPS && k < popups.size();)
		{
			if(popups[k].player != popup.player) { ++k; continue; }
			if(!popups[k].read)
			{
				++iDroppedUnread;
				if(!popups[k].dialog.empty()) dialogsToClose += popups[k].dialog + "\n";
			}
			popups.erase(popups.begin() + k);
			--iKept;
		}
		LeaveCriticalSection(&lock);
		if(iDroppedUnread > 0) ClaudeBridge::Get().LogLineFor(popup.player, CLAUDE_LOG_MESSAGES, ClaudeFormat("[bridge] %d unread game message(s) dropped: more than %d were kept", iDroppedUnread, CLAUDE_MAX_POPUPS));
		ClaudeBridge::Get().LogLineFor(popup.player, CLAUDE_LOG_MESSAGES, ClaudeFormat("Game message #%d: ", popup.id) + popup.text);
	}
}

// Keeps pendingPopups: the UI may already have reported the new game's first popups.
// Sessions keep their names but lose their seats: the slots belong to the new game.
void ClaudeBridge::Impl::ResetForNewGame()
{
	EnterCriticalSection(&lock);
	gameSeed = CvPreGame::mapRandomSeed();
	popups.clear();
	popupSeq = 0;
	dialogsToClose.clear();
	testingMode = false;
	for(std::map<std::string, Session>::iterator it = sessions.begin(); it != sessions.end(); ++it)
	{
		if(it->second.info.seat >= 0) it->second.info.seat = CLAUDE_SEAT_LOST;
		it->second.diploCount = 0;
	}
	LeaveCriticalSection(&lock);
	ClaudeGameId();
	ClaudeResetDiploRequests();
}

namespace
{
	bool s_retryRequested = false;
	const DWORD CLAUDE_RETRY_WINDOW_MS = 4000;
}

void ClaudeBridge::RetryLater() { s_retryRequested = true; }

// The log lines for a call are written after it ran, for the civilization the session
// played before the call or, when it had none, the one it plays after: a claim of an
// opponent civilization is then hidden with the rest of that civilization's lines.
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
	PlayerTypes ePlayerBefore = ClaudePlayerOf(ClaudeBridge::Get().Current());
	ClaudeCrashSetContext((pJob->tool + " " + picojson::value(pJob->args).serialize()).c_str());
	s_retryRequested = false;
	pJob->error.clear();
	pJob->result = pTool->handler(pJob->args, pJob->error);
	ClaudeCrashSetContext("");
	bool bRetry = false;
	if(s_retryRequested)
	{
		DWORD now = GetTickCount();
		if(bFirstTry)
		{
			pJob->retrying = true;
			pJob->firstTryMs = now;
		}
		bRetry = now - pJob->firstTryMs < CLAUDE_RETRY_WINDOW_MS;
	}
	if(!bLogged) return bRetry;
	ClaudeSession s = ClaudeBridge::Get().Current();
	int player = (ePlayerBefore != NO_PLAYER) ? (int)ePlayerBefore : (int)ClaudePlayerOf(s);
	std::string who = ClaudeIdentityLabel(s.identity);
	if(bFirstTry) Append(player, who, szCategory, ClaudeDescribeCall(*pTool, pJob->args));
	if(bRetry) return true;
	std::string outcome = pJob->error.empty() ? ClaudeDescribeResult(*pTool, pJob->result) : "Refused: " + pJob->error;
	if(!outcome.empty()) Append(player, who, szCategory, "    " + outcome);
	return false;
}

void ClaudeBridge::Impl::Append(int player, const std::string& who, const char* category, const std::string& text)
{
	int iTurn = GC.getGame().getGameTurn();
	// With nobody on screen a seat's line is hidden: the rule is absolute, the moment is transient.
	bool bTeammate = (player >= 0) ? (GC.getGame().getActivePlayer() != NO_PLAYER && ClaudeOnScreenTeam((PlayerTypes)player)) : who.empty();
	std::string civ = (player >= 0 && player != GC.getGame().getActivePlayer()) ? GET_PLAYER((PlayerTypes)player).getCivilizationShortDescription() : "";
	EnterCriticalSection(&lock);
	if(iTurn != lastLogTurn)
	{
		ClaudeLogEntry separator = { CLAUDE_LOG_ALWAYS, ClaudeFormat("---------- Turn %d ----------", iTurn), -1, "", "", true };
		log.push_back(separator);
		++logSeq;
		lastLogTurn = iTurn;
	}
	ClaudeLogEntry entry = { category, text, player, who, civ, bTeammate };
	log.push_back(entry);
	++logSeq;
	while(log.size() > CLAUDE_MAX_LOG_ENTRIES) log.pop_front();
	LeaveCriticalSection(&lock);

	FILogFile* pLog = LOGFILEMGR.GetLog("AIThinking.log", FILogFile::kDontTimeStamp);
	if(pLog) pLog->Msg("[%s] %s%s%s", category, who.c_str(), who.empty() ? "" : ": ", text.c_str());
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

	// The session is whatever Mcp-Session-Id the client sends; initialize starts a fresh
	// one and hands its id back. An id the server no longer knows (the game restarted)
	// simply starts a session under that id: the model then names itself again. A client
	// that never sends one shares the session with the empty id.
	std::string token;
	std::map<std::string, std::string>::const_iterator itSession = req.headers.find("mcp-session-id");
	if(itSession != req.headers.end()) token = itSession->second;
	const picojson::object& o = request.get<picojson::object>();
	picojson::object::const_iterator itMethod = o.find("method");
	if(itMethod != o.end() && itMethod->second.is<std::string>() && itMethod->second.get<std::string>() == "initialize")
	{
		token = "civ5-" + ClaudeNewUuid();
		res.sessionId = token;
	}
	Touch(token);

	bool bIsNotification = false;
	picojson::value response = HandleRpc(request, token, bIsNotification);
	Touch(token); // a long wait counts as presence
	if(bIsNotification)
	{
		res.status = 202;
		res.body.clear();
		return;
	}
	res.status = 200;
	res.body = response.serialize();
}

picojson::value ClaudeBridge::Impl::HandleRpc(const picojson::value& request, const std::string& token, bool& bIsNotification)
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
		    "Several models can play in one game, each on its own seat; status.other_ai_players names them. "
		    "When you reconnect, set_identity gives you back the seat you held; status.seats_waiting_for_a_player lists seats "
		    "other models left, and take_seat takes one the human assigns you. "
		    "In a game with seats a new connection must choose: take_seat with a civilization, teammate, opponent, "
		    "or human to play the civilization on screen; in a game without seats you play the civilization on screen. "
		    "If the human has not told you your role, ask them first and offer the choices: "
		    "play their civilization, a teammate (status.teammate_civs_you_could_play), or a secret opponent. "
		    "Claim a teammate or opponent with take_seat during their turn; civ=opponent lets the game pick one at random. "
		    "A claimed seat is off screen; your turn comes after the human's each round, so wait_for_my_turn may take as long as they do. "
		    "Other leaders talk to you through diplomacy requests (briefing.diplomacy_requests): answer them with respond_to_request. "
		    "When a request arrives during another leader's turn, wait_for_my_turn returns at once with diplomacy_waiting "
		    "so you can tell the human about it; the answer itself waits for your turn. "
		    "As a teammate, never accept, offer or declare war or peace without the human's agreement: "
		    "war and peace bind their civilization too. "
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
		picojson::value value = CallTool(token, itName->second.get<std::string>(), args, bIsError);

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

// Whether the session's turn is open, a new diplomacy request reached it, or it has no
// civilization to wait for (status then says what to do).
bool ClaudeBridge::Impl::SessionWakes(const std::string& token, int iDiploSeqAtStart)
{
	bool bMyTurn = false;
	int iDiploSeq = 0, iSeat = CLAUDE_SEAT_SCREEN;
	if(!Flags(token, bMyTurn, iDiploSeq, iSeat)) return true;
	return bMyTurn || iDiploSeq != iDiploSeqAtStart || iSeat == CLAUDE_SEAT_LOST || iSeat == CLAUDE_SEAT_UNDECIDED;
}

picojson::value ClaudeBridge::Impl::CallTool(const std::string& token, const std::string& name, const picojson::object& args, bool& bIsError)
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
		if(name == CLAUDE_TOOL_ADVANCE_ROUND) return AdvanceRound(token, args, bIsError);
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
		bool bMyTurn = false;
		int iSeqAtStart = 0, iSeat = 0;
		Flags(token, bMyTurn, iSeqAtStart, iSeat);
		while(!shuttingDown && !(GameRunning() && SessionWakes(token, iSeqAtStart)) && GetTickCount() - start < (DWORD)(timeout * 1000.0))
		{
			Sleep(250);
			Touch(token);
		}
	}

	std::string error;
	picojson::value result = RunOnGameThread(token, name, args, error);
	if(!error.empty())
	{
		bIsError = true;
		return picojson::value(error);
	}
	return result;
}

// Solo testing: one call per round instead of end_turn, wait, skip_human_turn, wait.
picojson::value ClaudeBridge::Impl::AdvanceRound(const std::string& token, const picojson::object& args, bool& bIsError)
{
	if(!testingMode) { bIsError = true; return picojson::value("advance_round is a testing tool: call testing_mode with enabled=true first"); }
	std::string error;
	int iTurnBefore = gameTurn;
	picojson::value r = RunOnGameThread(token, CLAUDE_TOOL_END_TURN, args, error);
	if(!error.empty()) { bIsError = true; return picojson::value(error); }
	DWORD start = GetTickCount();
	const DWORD limit = (DWORD)(CLAUDE_WAIT_MAX_SECONDS * 1000.0);
	bool bSkipped = false;
	bool bReopened = false;
	bool bTurnEnded = false;
	while(!shuttingDown && GetTickCount() - start < limit)
	{
		bool bMyTurn = false;
		int iDiploSeq = 0, iSeat = 0;
		Flags(token, bMyTurn, iDiploSeq, iSeat);
		if(!bTurnEnded)
		{
			// Give the turn a few seconds to close before watching for it to come back.
			if(!bMyTurn || GetTickCount() - start >= 5000) bTurnEnded = true;
		}
		else if(GameRunning() && bMyTurn) { bReopened = true; break; } // the turn came back (a unit arrived with moves left) or the round is done
		else if(!bSkipped && screenHumanTurn && !ClaudeBridge::Get().ScreenPlayed())
		{
			picojson::object none;
			RunOnGameThread(token, CLAUDE_TOOL_SKIP_HUMAN_TURN, none, error);
			if(!error.empty()) { bIsError = true; return picojson::value(error); }
			bSkipped = true;
		}
		Sleep(bTurnEnded ? 250 : 100);
		Touch(token);
	}
	picojson::object none;
	r = RunOnGameThread(token, CLAUDE_TOOL_STATUS, none, error);
	if(!error.empty()) { bIsError = true; return picojson::value(error); }
	if(r.is<picojson::object>())
	{
		picojson::object& o = r.get<picojson::object>();
		o["round_advanced"] = picojson::value(gameTurn > iTurnBefore);
		o["turn_reopened"] = picojson::value(gameTurn == iTurnBefore && bReopened);
	}
	return r;
}

picojson::value ClaudeBridge::Impl::RunOnGameThread(const std::string& token, const std::string& tool, const picojson::object& args, std::string& error)
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
	pJob->session = token;
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
