/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#pragma once

class Controller;
class SessionConnection;

class SessionManager
{
public:
	SessionManager(Controller *);
	virtual ~SessionManager();

	void start();
	void stop();

	void connectionStart(SessionConnection *);
	void connectionStop(SessionConnection *);

private:
	mutex mutex;

	Controller* controller;

	bool rundown;

	list<SessionConnection *> connectionList;

	int totalCnt;
	int connectedCnt;

	void collectLoop();

	volatile bool collectRun;
	std::thread *collectThread;
	std::deque<SessionConnection *> collectList;
	HANDLE collectEvent;

	int spareTarget;
};

