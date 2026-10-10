#include "CvGameCoreDLLPCH.h"
#include <deque>
#include <algorithm>
#include "ClaudeBridge.h"
#include "ClaudeHttp.h"
#include "ClaudeTools.h"
#include "../CvGame.h"
#include "../CvPlayer.h"
#include "../CvGlobals.h"

#define CLAUDE_PORT 7676
#define CLAUDE_JOB_TIMEOUT_MS 15000
#define CLAUDE_START_RETRY_MS 10000
#define CLAUDE_GAME_IDLE_MS 3000
#define CLAUDE_WAIT_DEFAULT_SECONDS 25.0
#define CLAUDE_WAIT_MAX_SECONDS 55.0
#define CLAUDE_MAX_LOG_ENTRIES 5000

struct ClaudeJob
{
	std::string tool;
	picojson::object args;
	picojson::value result;
	std::string error;
	bool done;
	bool abandoned;
	HANDLE event;

	ClaudeJob() : done(false), abandoned(false), event(CreateEvent(NULL, TRUE, FALSE, NULL)) {}
	~ClaudeJob() { CloseHandle(event); }
};

namespace
{
	void DiagLog(const std::string& msg)
	{
		FILogFile* pLog = LOGFILEMGR.GetLog("ClaudePlayer.log", FILogFile::kDontTimeStamp);
		if(pLog) pLog->Msg(msg.c_str());
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
	std::vector<std::string> log;
	int logSeq;
	int lastLogTurn;

	// Written by the game thread, read by the HTTP thread.
	volatile bool shuttingDown;
	volatile bool myTurn;
	volatile DWORD lastTickMs;

	Impl();
	~Impl();

	// game thread
	void Tick();
	void TryStart();
	void UpdateTurnFlags();
	void RunJob(ClaudeJob* pJob);
	void LogLine(const std::string& text);

	// any thread
	int GetLogText(std::string& out);
	bool GameRunning() const { return lastTickMs != 0 && GetTickCount() - lastTickMs < CLAUDE_GAME_IDLE_MS; }

	// HTTP thread
	static void HttpHandler(const ClaudeHttpRequest& req, ClaudeHttpResponse& res);
	void HandleHttp(const ClaudeHttpRequest& req, ClaudeHttpResponse& res);
	picojson::value HandleRpc(const picojson::value& request, bool& bIsNotification);
	picojson::value CallTool(const std::string& name, const picojson::object& args, bool& bIsError);
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
void ClaudeBridge::LogLine(const std::string& text) { m_pImpl->LogLine(text); }
int ClaudeBridge::GetLogText(std::string& out) { return m_pImpl->GetLogText(out); }

ClaudeBridge::Impl::Impl()
	: pServer(NULL), lastStartAttemptMs(0), startFailureShown(false), logSeq(0), lastLogTurn(-1),
	  shuttingDown(false), myTurn(false), lastTickMs(0)
{
	InitializeCriticalSection(&lock);
}

ClaudeBridge::Impl::~Impl()
{
	shuttingDown = true;
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
	}
	DeleteCriticalSection(&lock);
}

void ClaudeBridge::Impl::TryStart()
{
	DWORD now = GetTickCount();
	if(lastStartAttemptMs != 0 && now - lastStartAttemptMs < CLAUDE_START_RETRY_MS) return;
	lastStartAttemptMs = now;

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
		LogLine(msg + " (retrying)");
	}
	delete pServer;
	pServer = NULL;
}

void ClaudeBridge::Impl::UpdateTurnFlags()
{
	PlayerTypes eActive = GC.getGame().getActivePlayer();
	myTurn = (eActive != NO_PLAYER) && ClaudeIsPlayerTurn(GET_PLAYER(eActive));
}

void ClaudeBridge::Impl::Tick()
{
	DWORD now = GetTickCount();
	lastTickMs = (now == 0) ? 1 : now;
	if(pServer == NULL) TryStart();
	UpdateTurnFlags();

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

		RunJob(pJob);
		UpdateTurnFlags();

		EnterCriticalSection(&lock);
		pJob->done = true;
		bool bAbandoned = pJob->abandoned;
		if(!bAbandoned) SetEvent(pJob->event);
		LeaveCriticalSection(&lock);
		if(bAbandoned) delete pJob;
	}
}

void ClaudeBridge::Impl::RunJob(ClaudeJob* pJob)
{
	const ClaudeToolDef* pTool = ClaudeFindTool(pJob->tool);
	if(pTool == NULL)
	{
		pJob->error = "unknown tool: " + pJob->tool;
		return;
	}

	pJob->result = pTool->handler(pJob->args, pJob->error);

	if(pTool->isAction)
	{
		std::string line = "> " + pJob->tool + " " + picojson::value(pJob->args).serialize();
		if(!pJob->error.empty()) line += "  => ERROR: " + pJob->error;
		LogLine(line);
	}
}

void ClaudeBridge::Impl::LogLine(const std::string& text)
{
	int iTurn = GC.getGame().getGameTurn();
	EnterCriticalSection(&lock);
	if(iTurn != lastLogTurn)
	{
		log.push_back(ClaudeFormat("---------- Turn %d ----------", iTurn));
		++logSeq;
		lastLogTurn = iTurn;
	}
	log.push_back(text);
	++logSeq;
	if(log.size() > CLAUDE_MAX_LOG_ENTRIES)
		log.erase(log.begin(), log.begin() + (log.size() - CLAUDE_MAX_LOG_ENTRIES));
	LeaveCriticalSection(&lock);

	FILogFile* pLog = LOGFILEMGR.GetLog("ClaudeThinking.log", FILogFile::kDontTimeStamp);
	if(pLog) pLog->Msg(text.c_str());
}

int ClaudeBridge::Impl::GetLogText(std::string& out)
{
	out.clear();
	EnterCriticalSection(&lock);
	for(size_t i = 0; i < log.size(); ++i)
	{
		if(i) out += '\n';
		out += log[i];
	}
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
		std::string version = "2025-06-18";
		picojson::object::const_iterator itV = params.find("protocolVersion");
		if(itV != params.end() && itV->second.is<std::string>() && !itV->second.get<std::string>().empty())
			version = itV->second.get<std::string>();

		picojson::object caps;
		caps["tools"] = picojson::value(picojson::object());
		picojson::object info;
		info["name"] = picojson::value("civ5-claude-player");
		info["version"] = picojson::value("0.1");
		picojson::object result;
		result["protocolVersion"] = picojson::value(version);
		result["capabilities"] = picojson::value(caps);
		result["serverInfo"] = picojson::value(info);
		result["instructions"] = picojson::value(
		    "You are playing Civilization V as the active human player. Call wait_for_my_turn until it is your turn, "
		    "then use the query tools, act, write your reasoning with log, and finish with end_turn.");
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
