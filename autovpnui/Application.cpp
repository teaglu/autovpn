/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 ******************************************************************************/

#include "pch.h"
#include "Log.h"

#include "Application.h"
#include "StatusDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(Application, CWinApp)
END_MESSAGE_MAP()

Application::Application() noexcept
{
	SetAppID(_T("com.teaglu.autovpn"));
}

Application theApp;

static void setupLogging()
{
	CString logPath;

	LPTSTR localAppDataLow;
	if (SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, NULL, &localAppDataLow) == S_OK) {
		logPath = localAppDataLow;
		CoTaskMemFree(localAppDataLow);
	} else {
		logPath = _T("C:\\Temp");
	}

	logPath.Append(_T("\\Teaglu"));
	if (!CreateDirectory(logPath.GetBuffer(0), NULL)) {
		if (GetLastError() != ERROR_ALREADY_EXISTS) {
			AfxMessageBox(_T("Unable to create log directory in profile"));
			ExitProcess(1);
		}
	}
	logPath.Append(_T("\\autovpn"));
	if (!CreateDirectory(logPath.GetBuffer(0), NULL)) {
		if (GetLastError() != ERROR_ALREADY_EXISTS) {
			AfxMessageBox(_T("Unable to create log directory in profile"));
			ExitProcess(1);
		}
	}

	logPath.Append(_T("\\ui.log"));

	WIN32_FIND_DATA findData;
	HANDLE findHandle = FindFirstFile(logPath.GetBuffer(0), &findData);
	if (findHandle != INVALID_HANDLE_VALUE) {
		// Don't let the file get too huge
		if ((findData.nFileSizeHigh > 0) || (findData.nFileSizeLow >= 1000000)) {
			(VOID)DeleteFile(logPath.GetBuffer(0));
		}
		FindClose(findHandle);
	}

	Log::init(logPath.GetBuffer(0));
}

BOOL Application::InitInstance()
{
	{
		// If we can find another copy of ourself we want to kick that one to
		// show its window, not launch two copies.
		HWND otherCopy = FindWindow(_T("Teaglu::AutoVPNUi"), NULL);
		if (otherCopy != NULL) {
			COPYDATASTRUCT cds;
			cds.dwData = CDS_RELAUNCH;
			cds.lpData = NULL;
			cds.cbData = 0;

			LRESULT lResult = ::SendMessage(otherCopy, WM_COPYDATA,
				(WPARAM)m_pMainWnd->GetSafeHwnd(), (LPARAM)&cds);

			if (!lResult) {
				CString sErr;
				sErr.Format(
					_T("Failed to send message to running copy: LRESULT=%d, W32ERR=%d"),
					lResult, ::GetLastError());

				AfxMessageBox(sErr, MB_ICONEXCLAMATION);
			}
			return FALSE;
		}
	}

	setupLogging();

	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	if (!AfxSocketInit()) {
		AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
		return FALSE;
	}

	if (!AfxOleInit()) {
		AfxMessageBox(IDP_OLE_INIT_FAILED);
		return FALSE;
	}

	AfxEnableControlContainer();

	{
		// Subclass Dialog and register the class so we can be found.  The other part
		// of this equation is the CLASS directive on the dialog resource.
		WNDCLASSEX wndClass;
		wndClass.cbSize = sizeof(WNDCLASSEX);
		GetClassInfoEx(0, WC_DIALOG, &wndClass);
		wndClass.lpszClassName = _T("Teaglu::AutoVPNUi");
		wndClass.style &= ~CS_GLOBALCLASS;
		RegisterClassEx(&wndClass);
	}

	// I'm not sure what this does
	EnableTaskbarInteraction(TRUE);

	CDialog* dialog = new StatusDlg();
	dialog->Create(IDD_STATUS, NULL);
	m_pMainWnd = dialog;

	return TRUE;
}

int Application::ExitInstance()
{

	if (m_pMainWnd != NULL) {
		StatusDlg* dialog = (StatusDlg*)m_pMainWnd;
		dialog->DestroyWindow();
	}

	return CWinApp::ExitInstance();
}


