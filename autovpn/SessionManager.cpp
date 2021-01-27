/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#include "pch.h"
#include "Log.h"

#include "Controller.h"
#include "SessionManager.h"
#include "SessionConnection.h"

SessionManager::SessionManager(Controller *controller)
{
	this->controller = controller;
	collectRun = false;
	collectThread = NULL;

	totalCnt= 0;
	connectedCnt= 0;

	spareTarget= 1;
	rundown= false;

	collectEvent= CreateEvent(NULL, FALSE, FALSE, NULL);
}

SessionManager::~SessionManager()
{
	CloseHandle(collectEvent);
}

void SessionManager::connectionStart(SessionConnection *)
{
	std::unique_lock<std::mutex> lock(mutex);

	connectedCnt++;
	if (!rundown && ((totalCnt - connectedCnt) < spareTarget)) {
		SessionConnection *conn= new SessionConnection(this, controller);
		connectionList.push_back(conn);
		conn->start(false);
		totalCnt++;
	}
}

void SessionManager::connectionStop(SessionConnection * conn)
{
	bool stayAlive= false;
	std::unique_lock<std::mutex> lock(mutex);

	connectedCnt--;

	if (rundown || ((totalCnt - connectedCnt) > spareTarget)) {
		collectList.push_back(conn);
		SetEvent(collectEvent);
	}
}

void SessionManager::collectLoop()
{
	while (collectRun) {
		Log::log(LOG_DEBUG, _T("Waiting to collect things"));
		DWORD waitVal= ::WaitForSingleObject(collectEvent, INFINITE);
		if (waitVal != WAIT_OBJECT_0) {
			Log::log(LOG_ERROR,
				_T("Session manager collection thread event wait failed: {w32err}"));
			collectRun= false;
		}

		Log::log(LOG_DEBUG, _T("Starting collection loop"));
		std::unique_lock<std::mutex> lock(mutex);
		while (!collectList.empty()) {
			SessionConnection *conn= collectList.front();
			collectList.pop_front();
			totalCnt--;

			Log::log(LOG_DEBUG, _T("Stopping session connection %p"), conn);
			conn->stop();
			Log::log(LOG_DEBUG, _T("Stop complete on %p"), conn);

			connectionList.remove(conn);
			delete conn;
		}
		Log::log(LOG_DEBUG, _T("Collection loop done"));
	}

	Log::log(LOG_DEBUG, _T("Session connection collection thread exiting"));
}

void SessionManager::start()
{
	collectRun= true;
	collectThread= new std::thread(&SessionManager::collectLoop, this);

	while ((totalCnt - connectedCnt) < spareTarget) {
		SessionConnection *conn= new SessionConnection(this, controller);
		connectionList.push_back(conn);
		conn->start(totalCnt == 0);
		totalCnt++;
	}

	Log::log(LOG_DEBUG, _T("Started pipe"));
}

void SessionManager::stop()
{
	collectRun= false;
	SetEvent(collectEvent);
	collectThread->join();

	delete collectThread;
	collectThread = NULL;

	std::unique_lock<std::mutex> lock(mutex);

	rundown= true;
	for (auto conn : connectionList) {
		Log::log(LOG_DEBUG, _T("Late stop - what happened to collect loop?"));
		conn->stop();
		delete conn;
	}
}
