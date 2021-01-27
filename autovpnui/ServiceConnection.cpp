/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 ******************************************************************************/

#include "pch.h"
#include "Log.h"
#include "ServiceConnection.h"

DWORD ServiceConnection::CDS_CONN_DATA=			0x52f871B0;
DWORD ServiceConnection::CDS_CONN_ONLINE=		0xa5587b34;
DWORD ServiceConnection::CDS_CONN_OFFLINE=		0x1893baff;

ServiceConnection::ServiceConnection()
{
	thread= NULL;
	pipe= INVALID_HANDLE_VALUE;
	copyDataDest= NULL;

	stopEvent= ::CreateEvent(NULL, TRUE, FALSE, NULL);
}

ServiceConnection::~ServiceConnection()
{
	::CloseHandle(stopEvent);
}

void ServiceConnection::setCopyDataDest(HWND copyDataDest)
{

	this->copyDataDest= copyDataDest;
}

void ServiceConnection::start()
{
	::ResetEvent(stopEvent);
	thread= new std::thread(&ServiceConnection::mainLoop, this);
}

void ServiceConnection::stop()
{
	::SetEvent(stopEvent);
	thread->join();
}

#define READ_BUFFER_SIZE 2047

void ServiceConnection::mainLoop()
{
	static LPTSTR pipeName= _T("\\\\.\\pipe\\teaglu_autovpn");

	for (bool run= true; run; ) {
		bool readRun= false;
		{
			std::unique_lock<std::mutex> lock(mutex);

			Log::log(LOG_INFO, _T("Attempting connection to host service"));
			pipe= ::CreateFile(pipeName,
				GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

			if (pipe != INVALID_HANDLE_VALUE) {
				Log::log(LOG_INFO, _T("Connected to host service"));
				readRun= true;

				DWORD mode= PIPE_READMODE_MESSAGE;
				if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
					Log::log(LOG_ERROR, _T("Error setting PIPE_READMODE_MESSAGE: {w32err}"));
				}
			} else {
				Log::log(LOG_ERROR,
					_T("Error connecting to %s: {w32err}"), pipeName);
			}
		}

		if (readRun && run) {
			Log::log(LOG_INFO, _T("Handshaking to server"));

			BYTE helloMessage[8]={ 0x43, 0x81, 0xa9, 0x17, 0xee, 0xf2, 0x01, 0x92 };
			DWORD bytesWritten= 0;

			if (!WriteFile(pipe, helloMessage, sizeof(helloMessage), &bytesWritten, NULL)) {
				Log::log(LOG_ERROR,
					_T("Failed to write hello message on host socket: {w32err}"));
				readRun= false;
			} else if (bytesWritten != sizeof(helloMessage)) {
				Log::log(LOG_ERROR,
					_T("Unexpected write count on hello message to host socket: {w32err}"));
				readRun = false;
			} else {
				// While we have the mutex, if there is a listener then notify them that
				// the connection is invalid.
				if (copyDataDest != NULL) {
					COPYDATASTRUCT cds;
					cds.dwData= CDS_CONN_ONLINE;
					cds.cbData= 0;
					cds.lpData= NULL;

					::SendMessage(copyDataDest, WM_COPYDATA, (WPARAM)copyDataDest, (LPARAM)&cds);
				}
			}
		}

		HANDLE pipeEvent= CreateEvent(NULL, FALSE, FALSE, NULL);

		while (readRun && run) {
			char buffer[READ_BUFFER_SIZE + 1];
			DWORD bufferLen= 0;

			OVERLAPPED overlapped= { 0 };
			overlapped.hEvent= pipeEvent;

			bool processData= false;

			if (::ReadFile(pipe, buffer, READ_BUFFER_SIZE, &bufferLen, &overlapped)) {
				processData= true;
			} else if (GetLastError() == ERROR_IO_PENDING) {
				HANDLE events[2];
				events[0]= pipeEvent;
				events[1]= stopEvent;

				DWORD waitVal= ::WaitForMultipleObjects(2, events, FALSE, INFINITE);
				if (waitVal == WAIT_OBJECT_0) {
					if (GetOverlappedResult(pipe, &overlapped, &bufferLen, FALSE)) {
						processData= true;
					} else {
						Log::log(LOG_ERROR, _T("Error in host connection overlapped result: {w32err}"));
						readRun= false;
					}
				} else if (waitVal == (WAIT_OBJECT_0 + 1)) {
					run= false;
					readRun= false;
				} else {
					Log::log(LOG_ERROR,
						_T("Error in host connection WaitForMultipleObjects: {w32err}"));
					readRun= false;
				}
			} else {
				Log::log(LOG_ERROR,
					_T("Error reading pipe connection to host service: {w32err}"));
			}

			if (processData) {
				COPYDATASTRUCT cds;
				cds.dwData= CDS_CONN_DATA;
				cds.cbData= bufferLen;
				cds.lpData= buffer;

				// Throw the message safely over to the message loop
				::SendMessage(copyDataDest, WM_COPYDATA, (WPARAM)copyDataDest, (LPARAM)&cds);
			}
		}

		CloseHandle(pipeEvent);

		{
			std::unique_lock<std::mutex> lock(mutex);
			::CloseHandle(pipe);
			pipe= INVALID_HANDLE_VALUE;

			// While we have the mutex, if there is a listener then notify them that
			// the connection is invalid.
			if (copyDataDest != NULL) {
				COPYDATASTRUCT cds;
				cds.dwData= CDS_CONN_OFFLINE;
				cds.cbData= 0;
				cds.lpData= NULL;

				::SendMessage(copyDataDest, WM_COPYDATA, (WPARAM)copyDataDest, (LPARAM)&cds);
			}
		}

		if (run) {
			// Sleep to retry
			DWORD waitVal= ::WaitForSingleObject(stopEvent, 5000);
			if (waitVal == WAIT_OBJECT_0) {
				run= false;
			} else if (waitVal != WAIT_TIMEOUT) {
				Log::log(LOG_ERROR,
					_T("Error in WaitForSingleObject waiting for retry: {w32err}"));
			}
		}
	}
}

bool ServiceConnection::send(char *data, int dataLen)
{
	bool rval= false;
	std::unique_lock<std::mutex> lock(mutex);

	if (pipe != INVALID_HANDLE_VALUE) {
		DWORD bytesWritten= 0;

		if (::WriteFile(pipe, data, dataLen, &bytesWritten, NULL)) {
			if (bytesWritten == dataLen) {
				rval= true;
			}
		} else {
			Log::log(LOG_ERROR,
				_T("Error sending message to host service: {w32err}"));
		}
	}

	return rval;
}
