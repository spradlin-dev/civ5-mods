#pragma once

#include <string>
#include <map>

// Minimal HTTP/1.1 server on a background thread, serving one connection at a
// time. Knows nothing about the game; every request is handed to the handler
// callback, which runs on the server thread.
struct ClaudeHttpRequest
{
	std::string method;
	std::string path;
	std::map<std::string, std::string> headers; // lower-case names
	std::string body;
};

struct ClaudeHttpResponse
{
	int status;
	std::string contentType;
	std::string body;

	ClaudeHttpResponse() : status(200), contentType("application/json") {}
};

typedef void (*ClaudeHttpHandler)(const ClaudeHttpRequest& req, ClaudeHttpResponse& res);

class ClaudeHttpServer
{
public:
	ClaudeHttpServer();
	~ClaudeHttpServer();

	// Binds 127.0.0.1:port exclusively and starts the accept loop. Returns false
	// on failure; lastError() then describes why.
	bool Start(unsigned short port, ClaudeHttpHandler handler);

	// Closes the listening and any in-flight connection, then waits up to
	// joinTimeoutMs for the thread. The handler must already be unblocked.
	void Stop(unsigned long joinTimeoutMs);

	const std::string& lastError() const { return m_lastError; }

private:
	static unsigned long __stdcall ThreadMain(void* arg);
	void Run();
	void ServeConnection(unsigned int socket);

	unsigned int m_listenSocket;
	volatile unsigned int m_clientSocket;
	void* m_thread;
	volatile bool m_stop;
	ClaudeHttpHandler m_handler;
	std::string m_lastError;
};
