/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#include "pch.h"
#include "Message.h"
#include "Controller.h"
#include "SessionConnection.h"
#include "SessionManager.h"
#include "Settings.h"
#include "Log.h"

SessionConnection::SessionConnection(SessionManager *manager, Controller *autoVPN)
{
	this->manager= manager;
	this->autoVPN= autoVPN;

	sessionThread= NULL;
	first= false;
	stopEvent= CreateEvent(NULL, TRUE, FALSE, NULL);
	sendPipe = INVALID_HANDLE_VALUE;

	lastSuggestionTime = (time_t)0;
}

SessionConnection::~SessionConnection()
{
	CloseHandle(stopEvent);
}

#define INBUFFER_SIZE 4096
#define OUTBUFFER_SIZE 4096

LPSECURITY_ATTRIBUTES SessionConnection::buildSecurityAttributes()
{
	LPSECURITY_ATTRIBUTES sa= (LPSECURITY_ATTRIBUTES)HeapAlloc(
		GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SECURITY_ATTRIBUTES));

	PSECURITY_DESCRIPTOR securityDescriptor= (PSECURITY_DESCRIPTOR)
		HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, SECURITY_DESCRIPTOR_MIN_LENGTH);

	SID_IDENTIFIER_AUTHORITY ntAuthority= SECURITY_NT_AUTHORITY;

	PSID anonymousSid= NULL;
	PTOKEN_USER userToken= NULL;

	PACL acl= NULL;

	bool success= false;
	do {
		if (!InitializeSecurityDescriptor(securityDescriptor, SECURITY_DESCRIPTOR_REVISION)) {
			Log::log(LOG_ERROR,
				_T("Failed to initialize session pipe security descriptor: {w32err}"));
			break;
		}

		if (!AllocateAndInitializeSid(&ntAuthority, 1, SECURITY_INTERACTIVE_RID, 0, 0, 0, 0, 0, 0, 0, &anonymousSid)) {
			Log::log(LOG_ERROR,
				_T("Failed to allocate SID for session pipe: {w32err}"));
			break;
		}

		HANDLE processToken;
		if( !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &processToken) )
		{
			Log::log(LOG_ERROR,
				_T("Failed to open current process token: {w32err}"));
			break;
		}

		DWORD userTokenLen;
		if (GetTokenInformation(processToken, TokenUser, userToken, 0, &userTokenLen)) {
			Log::log(LOG_ERROR,
				_T("Failed to load token information for sesion pipe: {w32err}"));
			CloseHandle(processToken);
			break;
		} else {
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
				Log::log(LOG_ERROR,
					_T("Failed to retrieve size for token information for session pipe: {w32err}"));
				CloseHandle(processToken);
				break;
			}
		}

		userToken = (PTOKEN_USER)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, userTokenLen);
		if (!GetTokenInformation(processToken, TokenUser, userToken, userTokenLen, &userTokenLen)) {
			Log::log(LOG_ERROR,
				_T("Failed to retrieve token information for sesion pipe: {w32err}"));
			CloseHandle(processToken);
			break;
		}

		CloseHandle(processToken);

		PSID runningSid= userToken->User.Sid;
		
		DWORD aclSize= sizeof(ACL) + (2 * sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD)) + GetLengthSid(anonymousSid) + GetLengthSid(runningSid);

		acl= (PACL)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, aclSize);

		if (!InitializeAcl(acl, aclSize, ACL_REVISION)) {
			Log::log(LOG_ERROR,
				_T("Failed to initialize ACL structure for session pipe: {w32err}"));
			break;
		}

		if (!AddAccessAllowedAce(acl, ACL_REVISION, FILE_GENERIC_READ|FILE_GENERIC_WRITE, anonymousSid)) {
			Log::log(LOG_ERROR,
				_T("Failed to add anonymous ID to sesion pipe ACL: {w32err}"));
			break;
		}


		if (!AddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, runningSid)) {
			Log::log(LOG_ERROR,
				_T("Failed to add SID for self to session pipe: {w32err}"));
			break;
		}

		if (!SetSecurityDescriptorDacl(securityDescriptor, TRUE, acl, FALSE)) {
			Log::log(LOG_ERROR,
				_T("Failed to set security DACL on session pipe: {w32err}"));
			break;
		}

		sa->nLength= sizeof(SECURITY_ATTRIBUTES);
		sa->bInheritHandle= FALSE;
		sa->lpSecurityDescriptor= securityDescriptor;
		success= true;
	} while (false);

	if (anonymousSid) {
		FreeSid(anonymousSid);
	}
	if (userToken) {
		HeapFree(GetProcessHeap(), 0, userToken);
	}

	if (!success) {
		HeapFree(GetProcessHeap(), 0, securityDescriptor);
		HeapFree(GetProcessHeap(), 0, sa);
		sa= NULL;
	}

	return sa;
}

void SessionConnection::freeSecurityAttributes(LPSECURITY_ATTRIBUTES securityAttributes)
{
	HeapFree(GetProcessHeap(), 0, securityAttributes->lpSecurityDescriptor);
	HeapFree(GetProcessHeap(), 0, securityAttributes);
}

void SessionConnection::mainLoop()
{
	bool rval= false;

	for (bool run= true; run; ) {
		LPCTSTR pipeName= _T("\\\\.\\pipe\\teaglu_autovpn");

		LPSECURITY_ATTRIBUTES securityAttributes= NULL;

		// FIXME we can probably just do this once
		securityAttributes= buildSecurityAttributes();

		HANDLE pipe= ::CreateNamedPipe(pipeName,
			PIPE_ACCESS_DUPLEX | WRITE_DAC | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
			PIPE_UNLIMITED_INSTANCES,
			INBUFFER_SIZE,
			OUTBUFFER_SIZE,
			NMPWAIT_USE_DEFAULT_WAIT,
			securityAttributes);

		if (pipe == INVALID_HANDLE_VALUE) {
			Log::log(LOG_ERROR,
				_T("Error creating named pipe [%s] for session agent server {w32err}"),
				pipeName);
			run= false;
		}

		if (securityAttributes != NULL) {
			freeSecurityAttributes(securityAttributes);
		}

		if (!run) break;

		HANDLE pipeEvent= CreateEvent(NULL, FALSE, FALSE, NULL);

		OVERLAPPED overlapped= { 0 };
		overlapped.hEvent= pipeEvent;

		bool error= false;

		if (ConnectNamedPipe(pipe, &overlapped)) {
			manager->connectionStart(this);
			bool handlerOk= handleClient(pipe, pipeEvent);

			manager->connectionStop(this);
		} else if (GetLastError() == ERROR_IO_PENDING) {
			HANDLE events[2];
			events[0]= pipeEvent;
			events[1]= stopEvent;

			DWORD waitValue= ::WaitForMultipleObjects(2, events, FALSE, INFINITE);
			if (waitValue == WAIT_OBJECT_0) {
				DWORD bytesRead;	// Not used
				if (!GetOverlappedResult(pipe, &overlapped, &bytesRead, FALSE)) {
					Log::log(LOG_ERROR, _T("Deferred error in ConnectNamedPipe: {w32err}"));
				} else {
					manager->connectionStart(this);
					if (!handleClient(pipe, pipeEvent)) {
						// false return means shutdown was called
						Log::log(LOG_DEBUG, _T("Got shutdown return from client loop"));
						run= false;
					} else {
						// If we're running down don't call connectionStop() or we get a deadlock
						manager->connectionStop(this);
					}
				}
			} else if (waitValue == (WAIT_OBJECT_0 + 1)) {
				// Stop handle
				run= false;
			} else {
				Log::log(LOG_ERROR, _T("SessionConnection: error in WaitForMultipleObjects: {w32err}"));
				run= false;
			}
		} else {
			Log::log(LOG_ERROR, _T("Error in ConnectNamedPipe: {w32err}"));
			run= false;
		}

		::CloseHandle(pipeEvent);

		::FlushFileBuffers(pipe);
		::DisconnectNamedPipe(pipe);
		::CloseHandle(pipe);
	}
}

bool SessionConnection::handleClient(HANDLE pipe, HANDLE pipeEvent)
{
	bool rval= true;

	Log::log(LOG_DEBUG, _T("Got connection"));

	bool helloReceived= false;
	for (bool run= true; run; ) {
		// Data handle
		BYTE buffer[INBUFFER_SIZE + 1];
		DWORD bufferLen= 0;

		bool processData= false;

		OVERLAPPED overlapped= { 0 };
		overlapped.hEvent= pipeEvent;

		if (ReadFile(pipe, buffer, INBUFFER_SIZE, (LPDWORD)&bufferLen, &overlapped)) {
			processData= true;
		} else if (GetLastError() == ERROR_IO_PENDING) {
			HANDLE events[2];
			events[0]= pipeEvent;
			events[1]= stopEvent;

			DWORD waitValue= ::WaitForMultipleObjects(2, events, FALSE, INFINITE);
			//Log::log(LOG_DEBUG, "Got async return (%d)", GetCurrentThreadId());
			if (waitValue == WAIT_OBJECT_0) {
				if (!GetOverlappedResult(pipe, &overlapped, &bufferLen, FALSE)) {
					if (GetLastError() != ERROR_BROKEN_PIPE) {
						Log::log(LOG_ERROR, _T("Failed getting overlapped result: {w32err}"));
					}
					run= false;
				} else {
					processData= true;
				}
			} else if (waitValue == (WAIT_OBJECT_0 + 1)) {
				// Stop handle
				run= false;
				rval= false;
			} else {
				Log::log(LOG_ERROR, _T("SessionConnection: error in client WaitForMultipleObjects: {w32err}"));
				run= false;
			}
		} else {
			if (GetLastError() != ERROR_BROKEN_PIPE) {
				Log::log(LOG_ERROR,
					_T("Error reading pipe message: {w32err}"));
			}
			run= false;
		}

		if (processData) {
			if (!helloReceived) {
				BYTE helloMessage[8]={ 0x43, 0x81, 0xa9, 0x17, 0xee, 0xf2, 0x01, 0x92 };

				bool match= false;
				if (bufferLen == 8) {
					match= true;
					for (int i= 0; match && (i < 8); i++) {
						if (helloMessage[i] != buffer[i]) {
							match= false;
						}
					}
				}

				if (!match) {
					Log::log(LOG_ERROR, _T("Client did not handshake correctly"));
				} else {
					helloReceived= true;

					{
						unique_lock<mutex> permit(sendLock);
						sendPipe = pipe;
					}

					autoVPN->registerStatusListener(this);
				}
			} else {
				buffer[bufferLen]= '\0';
				processMessage((char *)buffer, bufferLen);
			}
		}
	}

	if (helloReceived) {
		autoVPN->unregisterStatusListener(this);
	}

	unique_lock<mutex> permit(sendLock);
	sendPipe = INVALID_HANDLE_VALUE;

	return rval;
}

void SessionConnection::processMessage(char *buffer, int bufferLen)
{
	// Do stuff and/or things?
}

void SessionConnection::sendMessage(char type, void *data, size_t length)
{
	if (length > OUTBUFFER_SIZE) {
		Log::log(LOG_ERROR,
			_T("Attempt to send %d bytes on pipe, which is beyond OUTBUFFER_SIZE"),
			length);
		return;
	}

	unique_lock<mutex> permit(sendLock);

	if (sendPipe != INVALID_HANDLE_VALUE) {
		DWORD messageLen = sizeof(AutoVPNHeader) + (DWORD)length;
		BYTE *message = new BYTE[messageLen];
		AutoVPNHeader* header = (AutoVPNHeader*)message;

		// This is all named pipes on the same host, so we don't have to
		// worry about htons etc.

		header->version = AV_VERSION;
		header->opcode = type;
		header->length = (short)(length & 0xFFFF);

		CopyMemory(&message[sizeof(AutoVPNHeader)], data, length);

		DWORD bytesWritten;
		if (!WriteFile(sendPipe, message, messageLen, &bytesWritten, NULL)) {
			Log::log(LOG_ERROR,
				_T("Error writing message to pipe: {w32err}"));
		}

		delete[] message;
	}
}

void SessionConnection::onStatusChanged(AutoVPNStatus *status)
{
	sendMessage(AV_MESSAGE_STATUS, status, sizeof(AutoVPNStatus));
}

void SessionConnection::onSuggestion(LPCTSTR suggestion)
{
	time_t now;
	time(&now);

	// Only send the same suggestion once in a row per 60 seconds
	bool send= true;
	if (lastSuggestion.Compare(suggestion) == 0) {
		if (lastSuggestionTime > 0) {
			if ((now - lastSuggestionTime) < 60) {
				send = false;
			}
		}
	}

	if (send) {
		// Send the (TCHAR)0 through as well to make things easier
		sendMessage(AV_MESSAGE_SUGGESTION,
			(void *)suggestion, (wcslen(suggestion) + 1) * sizeof(TCHAR));

		lastSuggestion = suggestion;
		lastSuggestionTime = now;
	}
}

void SessionConnection::start(bool first)
{
	this->first= first;
	ResetEvent(stopEvent);
	sessionThread= new thread(&SessionConnection::mainLoop, this);
}

void SessionConnection::stop()
{
	SetEvent(stopEvent);
	sessionThread->join();

	delete sessionThread;
	sessionThread = NULL;
}
