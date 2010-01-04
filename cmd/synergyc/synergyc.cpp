/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2002 Chris Schoeneman
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "CClient.h"
#include "CScreen.h"
#include "ProtocolTypes.h"
#include "Version.h"
#include "XScreen.h"
#include "CNetworkAddress.h"
#include "CSocketMultiplexer.h"
#include "CTCPSocketFactory.h"
#include "XSocket.h"
#include "CThread.h"
#include "CEventQueue.h"
#include "CFunctionEventJob.h"
#include "CFunctionJob.h"
#include "CLog.h"
#include "CString.h"
#include "CStringUtil.h"
#include "LogOutputters.h"
#include "CArch.h"
#include "XArch.h"

#include <cstring>
#include <iostream>

#define DAEMON_RUNNING(running_)
#if WINAPI_MSWINDOWS
#include "CArchMiscWindows.h"
#include "CMSWindowsScreen.h"
#include "CMSWindowsUtil.h"
#include "CMSWindowsClientTaskBarReceiver.h"
#include "resource.h"
#include <conio.h>
#include "CMSWindowsClientApp.h"
#undef DAEMON_RUNNING
#define DAEMON_RUNNING(running_) CArchMiscWindows::daemonRunning(running_)
#elif WINAPI_XWINDOWS
#include "CXWindowsScreen.h"
#include "CXWindowsClientTaskBarReceiver.h"
#include "CXWindowsClientApp.h"
#elif WINAPI_CARBON
#include "COSXScreen.h"
#include "COSXClientTaskBarReceiver.h"
#include "COSXClientApp.h"
#endif

// platform dependent name of a daemon
#if SYSAPI_WIN32
#define DAEMON_NAME "Synergy+ Client"
#define DAEMON_INFO "Allows another computer to share it's keyboard and mouse with this computer."
#elif SYSAPI_UNIX
#define DAEMON_NAME "synergyc"
#endif

typedef int (*StartupFunc)(int, char**);
static bool startClient();
static void parse(int argc, const char* const* argv);

#if WINAPI_MSWINDOWS
CMSWindowsClientApp app;
#elif WINAPI_XWINDOWS
CXWindowsClientApp app;
#elif WINAPI_CARBON
COSXClientApp app;
#endif

//
// program arguments
//

#define ARG CClientApp::CArgs::s_instance
CClientApp::CArgs* CClientApp::CArgs::s_instance = NULL;


//
// platform dependent factories
//

static
CScreen*
createScreen()
{
#if WINAPI_MSWINDOWS
	return new CScreen(new CMSWindowsScreen(false));
#elif WINAPI_XWINDOWS
	return new CScreen(new CXWindowsScreen(ARG->m_display, false, ARG->m_yscroll));
#elif WINAPI_CARBON
	return new CScreen(new COSXScreen(false));
#endif
}

static
CClientTaskBarReceiver*
createTaskBarReceiver(const CBufferedLogOutputter* logBuffer)
{
#if WINAPI_MSWINDOWS
	return new CMSWindowsClientTaskBarReceiver(
							CMSWindowsScreen::getInstance(), logBuffer);
#elif WINAPI_XWINDOWS
	return new CXWindowsClientTaskBarReceiver(logBuffer);
#elif WINAPI_CARBON
	return new COSXClientTaskBarReceiver(logBuffer);
#endif
}


//
// platform independent main
//

static CClient*					s_client          = NULL;
static CScreen*					s_clientScreen    = NULL;
static CClientTaskBarReceiver*	s_taskBarReceiver = NULL;
//static double					s_retryTime       = 0.0;
static bool						s_suspened        = false;

#define RETRY_TIME 1.0

static
void
updateStatus()
{
	s_taskBarReceiver->updateStatus(s_client, "");
}

static
void
updateStatus(const CString& msg)
{
	s_taskBarReceiver->updateStatus(s_client, msg);
}

static
void
resetRestartTimeout()
{
	// retry time can nolonger be changed
	//s_retryTime = 0.0;
}

static
double
nextRestartTimeout()
{
	// retry at a constant rate (Issue 52)
	return RETRY_TIME;

	/*
	// choose next restart timeout.  we start with rapid retries
	// then slow down.
	if (s_retryTime < 1.0) {
		s_retryTime = 1.0;
	}
	else if (s_retryTime < 3.0) {
		s_retryTime = 3.0;
	}
	else {
		s_retryTime = 5.0;
	}
	return s_retryTime;
	*/
}

static
void
handleScreenError(const CEvent&, void*)
{
	LOG((CLOG_CRIT "error on screen"));
	EVENTQUEUE->addEvent(CEvent(CEvent::kQuit));
}

static
CScreen*
openClientScreen()
{
	CScreen* screen = createScreen();
	EVENTQUEUE->adoptHandler(IScreen::getErrorEvent(),
							screen->getEventTarget(),
							new CFunctionEventJob(
								&handleScreenError));
	return screen;
}

static
void
closeClientScreen(CScreen* screen)
{
	if (screen != NULL) {
		EVENTQUEUE->removeHandler(IScreen::getErrorEvent(),
							screen->getEventTarget());
		delete screen;
	}
}

static
void
handleClientRestart(const CEvent&, void* vtimer)
{
	// discard old timer
	CEventQueueTimer* timer = reinterpret_cast<CEventQueueTimer*>(vtimer);
	EVENTQUEUE->deleteTimer(timer);
	EVENTQUEUE->removeHandler(CEvent::kTimer, timer);

	// reconnect
	startClient();
}

static
void
scheduleClientRestart(double retryTime)
{
	// install a timer and handler to retry later
	LOG((CLOG_DEBUG "retry in %.0f seconds", retryTime));
	CEventQueueTimer* timer = EVENTQUEUE->newOneShotTimer(retryTime, NULL);
	EVENTQUEUE->adoptHandler(CEvent::kTimer, timer,
							new CFunctionEventJob(&handleClientRestart, timer));
}

static
void
handleClientConnected(const CEvent&, void*)
{
	LOG((CLOG_NOTE "connected to server"));
	resetRestartTimeout();
	updateStatus();
}

static
void
handleClientFailed(const CEvent& e, void*)
{
	CClient::CFailInfo* info =
		reinterpret_cast<CClient::CFailInfo*>(e.getData());

	updateStatus(CString("Failed to connect to server: ") + info->m_what);
	if (!ARG->m_restartable || !info->m_retry) {
		LOG((CLOG_ERR "failed to connect to server: %s", info->m_what.c_str()));
		EVENTQUEUE->addEvent(CEvent(CEvent::kQuit));
	}
	else {
		LOG((CLOG_WARN "failed to connect to server: %s", info->m_what.c_str()));
		if (!s_suspened) {
			scheduleClientRestart(nextRestartTimeout());
		}
	}
	delete info;
}

static
void
handleClientDisconnected(const CEvent&, void*)
{
	LOG((CLOG_NOTE "disconnected from server"));
	if (!ARG->m_restartable) {
		EVENTQUEUE->addEvent(CEvent(CEvent::kQuit));
	}
	else if (!s_suspened) {
		s_client->connect();
	}
	updateStatus();
}

static
CClient*
openClient(const CString& name, const CNetworkAddress& address, CScreen* screen)
{
	CClient* client = new CClient(
		name, address, new CTCPSocketFactory, NULL, screen);

	try {
		EVENTQUEUE->adoptHandler(
			CClient::getConnectedEvent(),
			client->getEventTarget(),
			new CFunctionEventJob(handleClientConnected));

		EVENTQUEUE->adoptHandler(
			CClient::getConnectionFailedEvent(),
			client->getEventTarget(),
			new CFunctionEventJob(handleClientFailed));

		EVENTQUEUE->adoptHandler(
			CClient::getDisconnectedEvent(),
			client->getEventTarget(),
			new CFunctionEventJob(handleClientDisconnected));

	} catch (std::bad_alloc &ba) {
		delete client;
		throw ba;
	}

	return client;
}

static
void
closeClient(CClient* client)
{
	if (client == NULL) {
		return;
	}

	EVENTQUEUE->removeHandler(CClient::getConnectedEvent(), client);
	EVENTQUEUE->removeHandler(CClient::getConnectionFailedEvent(), client);
	EVENTQUEUE->removeHandler(CClient::getDisconnectedEvent(), client);
	delete client;
}

static
bool
startClient()
{
	double retryTime;
	CScreen* clientScreen = NULL;
	try {
		if (s_clientScreen == NULL) {
			clientScreen = openClientScreen();
			s_client     = openClient(ARG->m_name,
							*ARG->m_serverAddress, clientScreen);
			s_clientScreen  = clientScreen;
			LOG((CLOG_NOTE "started client"));
		}
		s_client->connect();
		updateStatus();
		return true;
	}
	catch (XScreenUnavailable& e) {
		LOG((CLOG_WARN "cannot open secondary screen: %s", e.what()));
		closeClientScreen(clientScreen);
		updateStatus(CString("Cannot open secondary screen: ") + e.what());
		retryTime = e.getRetryTime();
	}
	catch (XScreenOpenFailure& e) {
		LOG((CLOG_CRIT "cannot open secondary screen: %s", e.what()));
		closeClientScreen(clientScreen);
		return false;
	}
	catch (XBase& e) {
		LOG((CLOG_CRIT "failed to start client: %s", e.what()));
		closeClientScreen(clientScreen);
		return false;
	}

	if (ARG->m_restartable) {
		scheduleClientRestart(retryTime);
		return true;
	}
	else {
		// don't try again
		return false;
	}
}

static
void
stopClient()
{
	closeClient(s_client);
	closeClientScreen(s_clientScreen);
	s_client       = NULL;
	s_clientScreen = NULL;
}

static
int
mainLoop()
{
	// logging to files
	CFileLogOutputter* fileLog = NULL;

	if (ARG->m_logFile != NULL) {
		fileLog = new CFileLogOutputter(ARG->m_logFile);

		CLOG->insert(fileLog);

		LOG((CLOG_DEBUG1 "Logging to file (%s) enabled", ARG->m_logFile));
	}

	// create socket multiplexer.  this must happen after daemonization
	// on unix because threads evaporate across a fork().
	CSocketMultiplexer multiplexer;

	// create the event queue
	CEventQueue eventQueue;

	// start the client.  if this return false then we've failed and
	// we shouldn't retry.
	LOG((CLOG_DEBUG1 "starting client"));
	if (!startClient()) {
		return kExitFailed;
	}

	// run event loop.  if startClient() failed we're supposed to retry
	// later.  the timer installed by startClient() will take care of
	// that.
	CEvent event;
	DAEMON_RUNNING(true);
	EVENTQUEUE->getEvent(event);
	while (event.getType() != CEvent::kQuit) {
		EVENTQUEUE->dispatchEvent(event);
		CEvent::deleteData(event);
		EVENTQUEUE->getEvent(event);
	}
	DAEMON_RUNNING(false);

	// close down
	LOG((CLOG_DEBUG1 "stopping client"));
	stopClient();
	updateStatus();
	LOG((CLOG_NOTE "stopped client"));

	if (fileLog) {
		CLOG->remove(fileLog);
		delete fileLog;		
	}

	return kExitSuccess;
}

static
int
daemonMainLoop(int, const char**)
{
#if SYSAPI_WIN32
	CSystemLogger sysLogger(DAEMON_NAME, false);
#else
	CSystemLogger sysLogger(DAEMON_NAME, true);
#endif
	return mainLoop();
}

static
int
standardStartup(int argc, char** argv)
{
	if (!ARG->m_daemon) {
		ARCH->showConsole(false);
	}

	// parse command line
	parse(argc, argv);

	// daemonize if requested
	if (ARG->m_daemon) {
		return ARCH->daemonize(DAEMON_NAME, &daemonMainLoop);
	}
	else {
		return mainLoop();
	}
}

static
int
run(int argc, char** argv, ILogOutputter* outputter, StartupFunc startup)
{
	// general initialization
	ARG->m_serverAddress = new CNetworkAddress;
	ARG->m_pname         = ARCH->getBasename(argv[0]);

	// install caller's output filter
	if (outputter != NULL) {
		CLOG->insert(outputter);
	}

	// save log messages
	// use heap memory because CLog deletes outputters on destruction
	CBufferedLogOutputter* logBuffer = new CBufferedLogOutputter(1000);
	CLOG->insert(logBuffer, true);

	// make the task bar receiver.  the user can control this app
	// through the task bar.
	s_taskBarReceiver = createTaskBarReceiver(logBuffer);

	// run
	int result = startup(argc, argv);

	// done with task bar receiver
	delete s_taskBarReceiver;

	delete ARG->m_serverAddress;
	return result;
}


//
// command line parsing
//

#define BYE "\nTry `%s --help' for more information."

#if SYSAPI_WIN32
static
void 
exitPause(int code) 
{
	CString name;
	CArchMiscWindows::getParentProcessName(name);

	// if the user did not launch from the command prompt (i.e. it was launched
	// by double clicking, or through a debugger), allow user to read any error
	// messages (instead of the window closing automatically).
	if (name != "cmd.exe") {
		std::cout << std::endl << "Press any key to exit...";
		int c = _getch();
	}

	exit(code);
}
static void	(*bye)(int) = &exitPause;
#else
static void	(*bye)(int) = &exit;
#endif

static
void
version()
{
	LOG((CLOG_PRINT "%s %s, protocol version %d.%d\n%s",
							ARG->m_pname,
							kVersion,
							kProtocolMajorVersion,
							kProtocolMinorVersion,
							kCopyright));
}

static
void
help()
{
#if WINAPI_XWINDOWS
#  define USAGE_DISPLAY_ARG		\
" [--display <display>]"
#  define USAGE_DISPLAY_INFO	\
"      --display <display>  connect to the X server at <display>\n"
#else
#  define USAGE_DISPLAY_ARG
#  define USAGE_DISPLAY_INFO
#endif

	char buffer[2000];
	sprintf(
		buffer,
		"Usage: %s"
		" [--daemon|--no-daemon]"
		" [--debug <level>]"
		USAGE_DISPLAY_ARG
		" [--name <screen-name>]"
		" [--yscroll <delta>]"
		" [--restart|--no-restart]"
		" <server-address>"
		"\n\n"
		"Start the synergy mouse/keyboard sharing server.\n"
		"\n"
		"  -d, --debug <level>      filter out log messages with priorty below level.\n"
		"                           level may be: FATAL, ERROR, WARNING, NOTE, INFO,\n"
		"                           DEBUG, DEBUG1, DEBUG2.\n"
		USAGE_DISPLAY_INFO
		"  -f, --no-daemon          run the client in the foreground.\n"
		"*     --daemon             run the client as a daemon.\n"
		"  -n, --name <screen-name> use screen-name instead the hostname to identify\n"
		"                           ourself to the server.\n"
		"      --yscroll <delta>    defines the vertical scrolling delta, which is\n"
		"                           120 by default.\n"
		"  -1, --no-restart         do not try to restart the client if it fails for\n"
		"                           some reason.\n"
		"*     --restart            restart the client automatically if it fails.\n"
		"  -l  --log <file>         write log messages to file.\n"
		"  -h, --help               display this help and exit.\n"
		"      --version            display version information and exit.\n"
		"\n"
		"* marks defaults.\n"
		"\n"
		"The server address is of the form: [<hostname>][:<port>].  The hostname\n"
		"must be the address or hostname of the server.  The port overrides the\n"
		"default port, %d.\n"
		"\n"
		"Where log messages go depends on the platform and whether or not the\n"
		"client is running as a daemon.",
		ARG->m_pname, kDefaultPort
	);
	
	std::cout << buffer << std::endl;
}

static
bool
isArg(int argi, int argc, const char* const* argv,
				const char* name1, const char* name2,
				int minRequiredParameters = 0)
{
	if ((name1 != NULL && strcmp(argv[argi], name1) == 0) ||
		(name2 != NULL && strcmp(argv[argi], name2) == 0)) {
		// match.  check args left.
		if (argi + minRequiredParameters >= argc) {
			LOG((CLOG_PRINT "%s: missing arguments for `%s'" BYE,
								ARG->m_pname, argv[argi], ARG->m_pname));
			bye(kExitArgs);
		}
		return true;
	}

	// no match
	return false;
}

static
void
parse(int argc, const char* const* argv)
{
	// about these use of assert() here:
	// previously an /analyze warning was displayed if we only used assert and
	// did not return on failure. however, this warning does not appear to show
	// any more (could be because new compiler args have been added).
	// the asserts are programmer benefit only; the os should never pass 0 args,
	// because the first is always the binary name. the only way assert would 
	// evaluate to true, is if this parse function were implemented incorrectly,
	// which is unlikely because it's old code and has a specific use.
	// we should avoid using anything other than assert here, because it will
	// look like important code, which it's not really.
	assert(ARG->m_pname != NULL);
	assert(argv != NULL);
	assert(argc >= 1);

	// set defaults
	ARG->m_name = ARCH->getHostName();

	// parse options
	int i;
	for (i = 1; i < argc; ++i) {
		if (isArg(i, argc, argv, "-d", "--debug", 1)) {
			// change logging level
			ARG->m_logFilter = argv[++i];
		}

		else if (isArg(i, argc, argv, "-n", "--name", 1)) {
			// save screen name
			ARG->m_name = argv[++i];
		}

		else if (isArg(i, argc, argv, NULL, "--camp")) {
			// ignore -- included for backwards compatibility
		}

		else if (isArg(i, argc, argv, NULL, "--no-camp")) {
			// ignore -- included for backwards compatibility
		}

		else if (isArg(i, argc, argv, "-f", "--no-daemon")) {
			// not a daemon
			ARG->m_daemon = false;
		}

		else if (isArg(i, argc, argv, NULL, "--daemon")) {
			// daemonize
			ARG->m_daemon = true;
		}

#if WINAPI_XWINDOWS
		else if (isArg(i, argc, argv, "-display", "--display", 1)) {
			// use alternative display
			ARG->m_display = argv[++i];
		}
#endif

		else if (isArg(i, argc, argv, NULL, "--yscroll", 1)) {
			// define scroll 
			ARG->m_yscroll = atoi(argv[++i]);
		}
		
		else if (isArg(i, argc, argv, "-l", "--log", 1)) {
			ARG->m_logFile = argv[++i];
		}

		else if (isArg(i, argc, argv, "-1", "--no-restart")) {
			// don't try to restart
			ARG->m_restartable = false;
		}

		else if (isArg(i, argc, argv, NULL, "--restart")) {
			// try to restart
			ARG->m_restartable = true;
		}

		else if (isArg(i, argc, argv, "-z", NULL)) {
			ARG->m_backend = true;
		}

		else if (isArg(i, argc, argv, "-h", "--help")) {
			help();
			bye(kExitSuccess);
		}

		else if (isArg(i, argc, argv, NULL, "--version")) {
			version();
			bye(kExitSuccess);
		}

#if WINAPI_MSWINDOWS
		else if (isArg(i, argc, argv, NULL, "--service")) {
			const char* serviceAction = argv[++i];

			if (_stricmp(serviceAction, "install") == 0) {
				app.installService();
			}
			else if (_stricmp(serviceAction, "uninstall") == 0) {
				app.uninstallService();
			}
			else if (_stricmp(serviceAction, "start") == 0) {
				app.startService();
			}
			else if (_stricmp(serviceAction, "stop") == 0) {
				app.stopService();
			}
			else {
				LOG((CLOG_ERR "unknown service action: %s", serviceAction));
				bye(kExitArgs);
			}
			bye(kExitSuccess);
		}
#endif

		else if (isArg(i, argc, argv, "--", NULL)) {
			// remaining arguments are not options
			++i;
			break;
		}

		else if (argv[i][0] == '-') {
			LOG((CLOG_PRINT "%s: unrecognized option `%s'" BYE,
								ARG->m_pname, argv[i], ARG->m_pname));
			bye(kExitArgs);
		}

		else {
			// this and remaining arguments are not options
			break;
		}
	}

	// exactly one non-option argument (server-address)
	if (i == argc) {
		LOG((CLOG_PRINT "%s: a server address or name is required" BYE,
								ARG->m_pname, ARG->m_pname));
		bye(kExitArgs);
	}
	if (i + 1 != argc) {
		LOG((CLOG_PRINT "%s: unrecognized option `%s'" BYE,
								ARG->m_pname, argv[i], ARG->m_pname));
		bye(kExitArgs);
	}

	// save server address
	try {
		*ARG->m_serverAddress = CNetworkAddress(argv[i], kDefaultPort);
		ARG->m_serverAddress->resolve();
	}
	catch (XSocketAddress& e) {
		// allow an address that we can't look up if we're restartable.
		// we'll try to resolve the address each time we connect to the
		// server.  a bad port will never get better.  patch by Brent
		// Priddy.
		if (!ARG->m_restartable || e.getError() == XSocketAddress::kBadPort) {
			LOG((CLOG_PRINT "%s: %s" BYE,
								ARG->m_pname, e.what(), ARG->m_pname));
			bye(kExitFailed);
		}
	}

	// increase default filter level for daemon.  the user must
	// explicitly request another level for a daemon.
	if (ARG->m_daemon && ARG->m_logFilter == NULL) {
#if SYSAPI_WIN32
		if (CArchMiscWindows::isWindows95Family()) {
			// windows 95 has no place for logging so avoid showing
			// the log console window.
			ARG->m_logFilter = "FATAL";
		}
		else
#endif
		{
			ARG->m_logFilter = "NOTE";
		}
	}

	// set log filter
	if (!CLOG->setFilter(ARG->m_logFilter)) {
		LOG((CLOG_PRINT "%s: unrecognized log level `%s'" BYE,
								ARG->m_pname, ARG->m_logFilter, ARG->m_pname));
		bye(kExitArgs);
	}

	// identify system
	LOG((CLOG_INFO "%s Client on %s %s", kAppVersion, ARCH->getOSName().c_str(), ARCH->getPlatformName().c_str()));

#ifdef WIN32
#ifdef _AMD64_
	LOG((CLOG_WARN "This is an experimental x64 build of %s. Use it at your own risk.", kApplication));
#endif
#endif

	if (CLOG->getFilter() > CLOG->getConsoleMaxLevel()) {
		if (ARG->m_logFile == NULL) {
			LOG((CLOG_WARN "log messages above %s are NOT sent to console (use file logging)", 
				CLOG->getFilterName(CLOG->getConsoleMaxLevel())));
		}
	}
}


//
// platform dependent entry points
//

#if SYSAPI_WIN32
static bool				s_hasImportantLogMessages = false;

//
// CMessageBoxOutputter
//
// This class writes severe log messages to a message box
//

class CMessageBoxOutputter : public ILogOutputter {
public:
	CMessageBoxOutputter() { }
	virtual ~CMessageBoxOutputter() { }

	// ILogOutputter overrides
	virtual void		open(const char*) { }
	virtual void		close() { }
	virtual void		show(bool) { }
	virtual bool		write(ELevel level, const char* message);
};

bool
CMessageBoxOutputter::write(ELevel level, const char* message)
{
	// note any important messages the user may need to know about
	if (level <= CLog::kWARNING) {
		s_hasImportantLogMessages = true;
	}

	// FATAL and PRINT messages get a dialog box if not running as
	// backend.  if we're running as a backend the user will have
	// a chance to see the messages when we exit.
	if (!ARG->m_backend && level <= CLog::kFATAL) {
		MessageBox(NULL, message, ARG->m_pname, MB_OK | MB_ICONWARNING);
		return false;
	}
	else {
		return true;
	}
}

static
void
byeThrow(int x)
{
	CArchMiscWindows::daemonFailed(x);
}

static
int
daemonNTMainLoop(int argc, const char** argv)
{
	parse(argc, argv);
	ARG->m_backend = false;
	return CArchMiscWindows::runDaemon(mainLoop);
}

static
int
daemonNTStartup(int, char**)
{
	CSystemLogger sysLogger(DAEMON_NAME, false);
	bye = &byeThrow;
	return ARCH->daemonize(DAEMON_NAME, &daemonNTMainLoop);
}

static
int
foregroundStartup(int argc, char** argv)
{
	ARCH->showConsole(false);

	// parse command line
	parse(argc, argv);

	// never daemonize
	return mainLoop();
}

static
void
showError(HINSTANCE instance, const char* title, UINT id, const char* arg)
{
	CString fmt = CMSWindowsUtil::getString(instance, id);
	CString msg = CStringUtil::format(fmt.c_str(), arg);
	MessageBox(NULL, msg.c_str(), title, MB_OK | MB_ICONWARNING);
}

int main(int argc, char** argv) {

	app.m_daemonName = DAEMON_NAME;
	app.m_daemonInfo = DAEMON_INFO;
	app.m_instance = GetModuleHandle(NULL);

	if (app.m_instance) {
		return WinMain(app.m_instance, NULL, GetCommandLine(), SW_SHOWNORMAL);
	} else {
		return kExitFailed;
	}
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
	try {
		CArchMiscWindows::setIcons((HICON)LoadImage(instance,
									MAKEINTRESOURCE(IDI_SYNERGY),
									IMAGE_ICON,
									32, 32, LR_SHARED),
									(HICON)LoadImage(instance,
									MAKEINTRESOURCE(IDI_SYNERGY),
									IMAGE_ICON,
									16, 16, LR_SHARED));
		CArch arch(instance);
		CMSWindowsScreen::init(instance);
		CLOG;
		CThread::getCurrentThread().setPriority(-14);
		CClientApp::CArgs args;

		StartupFunc startup;
		if (!CArchMiscWindows::isWindows95Family()) {

			// WARNING: this may break backwards computability!
			// previously, we were assuming that the process is launched from the
			// service host when no arguments were passed. if we wanted to launch
			// from console or debugger, we had to remember to pass -f which was
			// always the first pitfall for new committers. now, we are able to
			// check using the new `wasLaunchedAsService` function, which is a
			// more elegant solution.
			if (CArchMiscWindows::wasLaunchedAsService()) {
				startup = &daemonNTStartup;
			} else {
				startup = &foregroundStartup;
				ARG->m_daemon = false;
			}
		} else {
			startup = &standardStartup;
		}

		// previously we'd send PRINT and FATAL output to a message box, but now
		// that we're using an MS console window for Windows, there's no need really
		//int result = run(__argc, __argv, new CMessageBoxOutputter, startup);
		int result = run(__argc, __argv, NULL, startup);

		delete CLOG;
		return result;
	}
	catch (XBase& e) {
		showError(instance, __argv[0], IDS_UNCAUGHT_EXCEPTION, e.what());
		//throw;
	}
	catch (XArch& e) {
		showError(instance, __argv[0], IDS_INIT_FAILED, e.what().c_str());
	}
	catch (...) {
		showError(instance, __argv[0], IDS_UNCAUGHT_EXCEPTION, "<unknown exception>");
	}
	return kExitFailed;
}

#elif SYSAPI_UNIX

int
main(int argc, char** argv)
{
	CArgs args;
	try {
		int result;
		CArch arch;
		CLOG;
		CArgs args;
		result = run(argc, argv, NULL, &standardStartup);
		delete CLOG;
		return result;
	}
	catch (XBase& e) {
		LOG((CLOG_CRIT "Uncaught exception: %s\n", e.what()));
		throw;
	}
	catch (XArch& e) {
		LOG((CLOG_CRIT "Initialization failed: %s" BYE, e.what().c_str()));
		return kExitFailed;
	}
	catch (...) {
		LOG((CLOG_CRIT "Uncaught exception: <unknown exception>\n"));
		throw;
	}
}

#else

#error no main() for platform

#endif
