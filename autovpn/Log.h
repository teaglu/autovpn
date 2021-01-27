/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#pragma once

typedef enum ELogLevel {
	LL_CRITICAL=	0x10,
	LL_ERROR=		0x08,
	LL_WARNING=		0x04,
	LL_INFO=		0x02,
	LL_DEBUG=		0x01
} ELogLevel;

typedef struct SLogInfo {
	ELogLevel eLevel;
	LPCTSTR pszSourceFile;
	LPCTSTR pszFunction;
	LONG lSourceLine;
	LONG lErrorNum;
} SLogInfo;

#define LOG_CRITICAL SLogInfo{ LL_CRITICAL, _T(__FILE__), _T(__FUNCTION__), __LINE__, 0 }
#define LOG_ERROR    SLogInfo{ LL_ERROR,    _T(__FILE__), _T(__FUNCTION__), __LINE__, 0 }
#define LOG_WARNING  SLogInfo{ LL_WARNING,  _T(__FILE__), _T(__FUNCTION__), __LINE__, 0 }
#define LOG_INFO     SLogInfo{ LL_INFO,     _T(__FILE__), _T(__FUNCTION__), __LINE__, 0 }
#define LOG_DEBUG    SLogInfo{ LL_DEBUG,    _T(__FILE__), _T(__FUNCTION__), __LINE__, 0 }

class Log
{
public:
	Log();
	virtual ~Log();

	static VOID init(LPCTSTR logfile);

	static VOID log(const SLogInfo &, LPCTSTR, ...);

protected:
	static CRITICAL_SECTION sLock;
	static HANDLE fileLog;
};
