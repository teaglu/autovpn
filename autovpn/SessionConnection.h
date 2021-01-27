/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#pragma once

class SessionManager;
class Controller;
class Controller::StatusListener;

class SessionConnection : public Controller::StatusListener
{
public:
	SessionConnection(SessionManager *manager, Controller *autoVPN);
	~SessionConnection();

	void start(bool first);
	void stop();

	void sendMessage(char type, void* data, size_t length);

	virtual void onStatusChanged(AutoVPNStatus* status);
	virtual void onSuggestion(LPCTSTR);

private:
	SessionManager *manager;
	Controller *autoVPN;

	HANDLE stopEvent;

	mutex sendLock;
	HANDLE sendPipe;

	// Don't send the same thing over and over
	CString lastSuggestion;
	time_t lastSuggestionTime;

	bool first;
	LPSECURITY_ATTRIBUTES buildSecurityAttributes();
	void freeSecurityAttributes(LPSECURITY_ATTRIBUTES);

	bool handleClient(HANDLE pipe, HANDLE pipeEvent);
	void mainLoop();

	void processMessage(char *message, int messageLen);

	thread *sessionThread;
};
