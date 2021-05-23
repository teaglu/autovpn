/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#include "pch.h"
#include "Log.h"
#include "Ip4Network.h"
#include "Controller.h"

static TCHAR serviceName[] = _T("AutoVPN");
static TCHAR serviceDescr[] = _T("Automatic VPN Control");
static TCHAR serviceLongDescr[] = _T("Starts and stops VPN based on a list of trusted subnets");

static SERVICE_STATUS_HANDLE servStatusHandle;
static SERVICE_STATUS servStatus;

static mutex autoVpnLock;
static Controller* autoVpn = NULL;

VOID WINAPI serviceControl(DWORD lOperation)
{
	switch (lOperation) {
	case SERVICE_CONTROL_STOP:
		servStatus.dwCurrentState = SERVICE_STOP_PENDING;
		servStatus.dwCheckPoint = 0;
		servStatus.dwWaitHint = 0;
		SetServiceStatus(servStatusHandle, &servStatus);

		{
			unique_lock<mutex> permit(autoVpnLock);
			if (autoVpn != NULL) {
				autoVpn->stop();
			}
		}

		// serviceStop will make the final transition to STOPPED

		break;

	case SERVICE_CONTROL_INTERROGATE:
		SetServiceStatus(servStatusHandle, &servStatus);
		break;
	}
}

VOID WINAPI serviceStart(DWORD argc, LPTSTR* argv)
{
	servStatus.dwServiceType = SERVICE_WIN32;
	servStatus.dwCurrentState = SERVICE_START_PENDING;
	servStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	servStatus.dwWin32ExitCode = 0;
	servStatus.dwServiceSpecificExitCode = 0;
	servStatus.dwCheckPoint = 0;
	servStatus.dwWaitHint = 0;

	servStatusHandle = RegisterServiceCtrlHandler(serviceName, serviceControl);

	if (servStatusHandle == (SERVICE_STATUS_HANDLE)0) {
		Log::log(LOG_ERROR, _T("RegisterServiceCtrlHandler failed: {w32err}"));
		return;
	}

	// Initialization complete - report running status. 
	servStatus.dwCurrentState = SERVICE_RUNNING;
	servStatus.dwCheckPoint = 0;
	servStatus.dwWaitHint = 0;
	SetServiceStatus(servStatusHandle, &servStatus);

	Log::log(LOG_DEBUG, _T("Service runloop starting"));

	{
		unique_lock<mutex> permit(autoVpnLock);
		autoVpn = new Controller();
	}
	autoVpn->main();
	{
		unique_lock<mutex> permit(autoVpnLock);
		delete autoVpn;
		autoVpn = NULL;
	}

	Log::log(LOG_DEBUG, _T("Service runloop complete"));

	servStatus.dwCurrentState = SERVICE_STOPPED;
	servStatus.dwCheckPoint = 0;
	servStatus.dwWaitHint = 0;
	SetServiceStatus(servStatusHandle, &servStatus);
}

static VOID registerService()
{
	SC_HANDLE hMgr;

	hMgr = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (hMgr == NULL) {
		Log::log(LOG_ERROR, _T("can't open service control manager: {w32err}"));
	} else {
		TCHAR path[_MAX_PATH + 1];
		if (GetModuleFileName(NULL, path, _MAX_PATH) > 0) {
			if (CreateService(hMgr, serviceName, serviceDescr, SC_MANAGER_CONNECT,
				SERVICE_WIN32_OWN_PROCESS,
				SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
				path, NULL, NULL, NULL, NULL, NULL)) {
				Log::log(LOG_INFO, _T("Installed Service Entry: %s"), path);

				SC_HANDLE hService = ::OpenService(hMgr, serviceName, SC_MANAGER_ALL_ACCESS);
				if (hService != NULL) {
					// This sets the long description that shows up in the control panel - it's not
					// an option on the CreateService call AFAIK
					SERVICE_DESCRIPTION longDescription;
					longDescription.lpDescription = serviceLongDescr;
					if (!ChangeServiceConfig2(hService, SERVICE_CONFIG_DESCRIPTION, &longDescription)) {
						Log::log(LOG_ERROR, _T("Error setting service long description: {w32err}"));
					}

					if (::StartService(hService, 0, NULL)) {
						Log::log(LOG_INFO, _T("Service Started"));
					} else {
						Log::log(LOG_ERROR, _T("Unable to start service - check login rights."));
					}

					::CloseServiceHandle(hService);
				} else {
					Log::log(LOG_ERROR, _T("Unable to open service handle: {w32err}"));
				}
			} else {
				Log::log(LOG_ERROR, _T("failed to install service: {w32err}"));
			}
		} else {
			printf("unable to get file path");
		}
		CloseServiceHandle(hMgr);
	}
}

static VOID unregisterService()
{
	SC_HANDLE hMgr = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (hMgr == NULL) {
		Log::log(LOG_ERROR, _T("can't open service control manager: {w32err}"));
	} else {
		SC_HANDLE hService = ::OpenService(hMgr, serviceName, SERVICE_ALL_ACCESS);
		if (hService != NULL) {
			if (::DeleteService(hService)) {
				Log::log(LOG_INFO, _T("successfully uninstalled the service"));
			} else {
				Log::log(LOG_ERROR, _T("unable to delete service"));
			}
			::CloseServiceHandle(hService);
		} else {
			Log::log(LOG_ERROR, _T("Cannot find service"));
		}
		::CloseServiceHandle(hMgr);
	}
}

BOOL WINAPI CtrlHandler(DWORD ctrlType)
{
	BOOL handled = FALSE;

	switch (ctrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		{
			unique_lock<mutex> permit(autoVpnLock);
			if (autoVpn != NULL) {
				autoVpn->stop();
			}
		}

		handled = TRUE;
		break;

	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
	default:
		break;
	}

	printf("GOT CONSOLE EVENT: %d\n", ctrlType);

	return handled;
}

static void setCurrentDirectory()
{
	TCHAR path[_MAX_PATH + 1];
	if (GetModuleFileName(NULL, path, _MAX_PATH) > 0) {
		TCHAR* tail = wcsrchr(path, '\\');
		if (tail != NULL) {
			*tail = '\0';
			SetCurrentDirectory(path);
		}
	}
}

int wmain(int argc, wchar_t **argv)
{
	setCurrentDirectory();
	Log::init((argc <= 1) ? _T("error.log") : (LPCTSTR)NULL);

	{
		// This is needed for EnableHostname to check DNS

		WSADATA winsockData;
		ZeroMemory(&winsockData, sizeof(WSADATA));
		int winsockStatus = WSAStartup(MAKEWORD(2, 2), &winsockData);
		if (winsockStatus != 0) {
			Log::log(LOG_ERROR,
				_T("Error 0x%08X starting Winsock"),
				winsockStatus);
		}
	}

	if (argc > 1) {
		if (wcscmp(argv[1], _T("/console")) == 0) {
			// Be nice and install a control-c handler
			SetConsoleCtrlHandler(CtrlHandler, TRUE);

			{
				unique_lock<mutex> permit(autoVpnLock);
				autoVpn = new Controller();
			}
			autoVpn->main();
			{
				unique_lock<mutex> permit(autoVpnLock);
				delete autoVpn;
				autoVpn = NULL;
			}

		} else if (wcscmp(argv[1], _T("/install")) == 0) {
			registerService();
		} else if (wcscmp(argv[1], _T("/uninstall")) == 0) {
			unregisterService();
		} else {
			Log::log(LOG_ERROR, _T("Unknown flag: %s"), argv[1]);
			::ExitProcess(1);
		}
	} else {
		// This means we were invoked by the service controller, so follow standard startup protocol
		SERVICE_TABLE_ENTRY asEntry[] = {
			{ serviceName,  serviceStart },
			{ NULL, NULL }
		};

		if (!StartServiceCtrlDispatcher(asEntry)) {
			Log::log(LOG_ERROR, _T("Error entering service dispatch: {w32err}"));
		}
	}

	WSACleanup();
}
