/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 ******************************************************************************/

#include "pch.h"
#include "Log.h"

CRITICAL_SECTION Log::sLock;
HANDLE Log::fileLog;

Log::Log()
{
}

Log::~Log()
{
}

VOID Log::init(LPTSTR logFile)
{
	::InitializeCriticalSection(&sLock);

	if (logFile == NULL) {
		fileLog = GetStdHandle(STD_ERROR_HANDLE);
	} else {
		fileLog = ::CreateFile(logFile,
			FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);

		if (fileLog == INVALID_HANDLE_VALUE) {
			fileLog = ::GetStdHandle(STD_ERROR_HANDLE);
		}
	}
}

#define FORMAT_BUFFER_LEN 1024

VOID Log::log(const SLogInfo &sInfo, LPTSTR format, ...)
{
	bool bDoLog = true;

	if (bDoLog) {
		SYSTEMTIME now;
		GetLocalTime(&now);

		TCHAR rawMessage[FORMAT_BUFFER_LEN];

		// First use vsnprintf to format given the arguments passed in
		va_list args;
		va_start(args, format);
		_vsntprintf_s(rawMessage,
			FORMAT_BUFFER_LEN - 1, FORMAT_BUFFER_LEN - 1, format, args);

		CString message(rawMessage);

		// Now do any special replacement sequences

		// This avoids having the whole sequence of calls to get the
		// text of the last windows error every time we check for an
		// error.  We can't use % sequenes because those are consumed by
		// vsnprintf.
		int offset;
		if ((offset = message.Find(_T("{w32err}"))) != -1) {
			DWORD lError = ::GetLastError();
			TCHAR win32Err[1024];

			DWORD size = ::FormatMessage(
				FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, lError, 0, win32Err, 1023, NULL);

			if (size == 0) {
				_tcprintf_s(win32Err, 1023, "Unknown Win32 Error Code: %08X", lError);
			}
			message.Delete(offset, 8);
			message.Insert(offset, win32Err);
		}

		// This is the same thing for WSA errors
		if ((offset = message.Find(_T("{wsaerr}"))) != -1) {
			DWORD lError = ::WSAGetLastError();
			TCHAR winsockErr[1024];

			DWORD size = ::FormatMessage(
				FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, lError, 0, winsockErr, 1023, NULL);

			if (size == 0) {
				_tcprintf_s(winsockErr, 1023, _T("Unknown Winsock Error Code: %08X"), lError);
			}
			message.Delete(offset, 8);
			message.Insert(offset, winsockErr);
		}

		// Now build a string for the log file with date/time, error level, etc
		TCHAR timestamp[64];

		_sntprintf_s(timestamp, 63, _T("%04d-%02d-%02d %02d:%02d:%02d "),
			now.wYear, now.wMonth, now.wDay,
			now.wHour, now.wMinute, now.wSecond);

		CString line(timestamp);

		switch (sInfo.eLevel) {
		case LL_CRITICAL:
			line.Append(_T("[CRITICAL]"));
			break;
		case LL_ERROR:
			line.Append(_T("[ERROR]"));
			break;
		case LL_WARNING:
			line.Append(_T("[WARNING]"));
			break;
		case LL_INFO:
			line.Append(_T("[INFO]"));
			break;
		case LL_DEBUG:
			line.Append(_T("[DEBUG]"));
			break;
		}

		/*
		if (sInfo.pszSourceFile != NULL) {
			char lineStr[16];
			sprintf_s(lineStr, 15, "%d", sInfo.lSourceLine);

			line.append(" {");
			line.append(sInfo.pszSourceFile);
			line.append(" ");
			line.append(sInfo.pszFunction);
			line.append(lineStr);
			line.append(" }");
		}
		*/

		line.Append(_T(" "));
		line.Append(message);

		line.TrimRight();

		line.Append(_T("\r\n"));			// Use CRLF so notepad works

		CT2A logLine(line.GetBuffer(0));
		line.ReleaseBuffer();

		::EnterCriticalSection(&sLock);

		DWORD logLineLen= (DWORD)strlen(logLine);
		DWORD lBytesWritten = 0;
		if (!WriteFile(fileLog, logLine, logLineLen, &lBytesWritten, NULL)) {
			// Wut?
			fprintf(stderr, "Error writing to log file!\r\n");
		}

		::LeaveCriticalSection(&sLock);
	}
}
