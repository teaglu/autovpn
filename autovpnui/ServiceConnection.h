/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 ******************************************************************************/

#pragma once

class ServiceConnection {
public:
	ServiceConnection();
	~ServiceConnection();

	void start();
	void stop();

	void setCopyDataDest(HWND);

	bool send(char *data, int dataLen);

public:
	static DWORD CDS_CONN_DATA;
	static DWORD CDS_CONN_ONLINE;
	static DWORD CDS_CONN_OFFLINE;

private:
	std::mutex mutex;
	std::string nodename;

	HWND copyDataDest;

	void mainLoop();

	HANDLE pipe;
	HANDLE stopEvent;
	std::thread *thread;
};

