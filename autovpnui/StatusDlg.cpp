/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 ******************************************************************************/

#include "pch.h"
#include "Application.h"
#include "StatusDlg.h"
#include "ServiceConnection.h"

// The auto-detect of changes in VS doesn't work here, so if you change this
// in autovpn project be sure to run a "clean".  -DAW 201120
#include "../autovpn/Message.h"

// StatusDlg dialog

#define TIMERID_BRINGTOFRONT		0x01
#define TIMERID_HIDEONSUCCESS		0x02

IMPLEMENT_DYNAMIC(StatusDlg, CDialogEx)

StatusDlg::StatusDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_STATUS, pParent)
{
	service = NULL;

	state = AVS_UNKNOWN;
	signalQuality = 0;
	rxRate = 0;
	txRate = 0;

	wifiProblem = false;
	visible = false;

	bringToFrontTimer = 0;
	hideOnSuccessTimer = 0;
}

StatusDlg::~StatusDlg()
{
}

void StatusDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(StatusDlg, CDialogEx)
	ON_BN_CLICKED(IDOK, &StatusDlg::OnDialogOk)
	ON_BN_CLICKED(IDCANCEL, &StatusDlg::OnDialogCancel)
	ON_WM_PAINT()
	ON_WM_COPYDATA()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
END_MESSAGE_MAP()

void StatusDlg::OnDialogOk()
{
	// We shouldn't get this unless the user presses enter for some reason
}

void StatusDlg::OnDialogCancel()
{
	if (visible) {
		ShowWindow(SW_HIDE);
		visible = false;
	}
}

void StatusDlg::PostNcDestroy()
{
	if (service != NULL) {
		service->stop();
		delete service;
		service = NULL;
	}

	CDialogEx::PostNcDestroy();
	delete this;
}

void StatusDlg::OnPaint()
{
	CPaintDC dc(this);

	RECT clientArea;
	GetClientRect(&clientArea);

	Paint(dc, clientArea);
}

void StatusDlg::BufferedPaint(CDC& screenDc, RECT &screenArea)
{
	// FIXME
	// I was trying to get double-buffering to avoid flicker, but I'm doing something
	// wrong here - I'll loop back to this later.  -DAW 201120

	RECT bufferArea;
	bufferArea.left = 0;
	bufferArea.right = screenArea.right - screenArea.left;
	bufferArea.top = 0;
	bufferArea.bottom = screenArea.bottom - screenArea.top;

	CDC bufferDc;
	bufferDc.CreateCompatibleDC(&screenDc);

	CBitmap buffer;
	buffer.CreateCompatibleBitmap(&screenDc, bufferArea.right, bufferArea.top);
	CBitmap* oldBitmap = bufferDc.SelectObject(&buffer);

	bufferDc.FillSolidRect(&bufferArea, RGB(255, 128, 0));
	Paint(bufferDc, bufferArea);

	screenDc.FillSolidRect(&screenArea, RGB(128, 128, 128));

	//screenDc.FillSolidRect(screenArea.left, screenArea.top, 30, 30, RGB(0, 0, 255));
	//screenDc.BitBlt(screenArea.left, screenArea.top, bufferArea.right, bufferArea.bottom, &bufferDc, 0, 0, SRCCOPY);
	screenDc.BitBlt(0, 0, 50, 50, &bufferDc, 0, 0, SRCCOPY);
	//screenDc.FillSolidRect(screenArea.left + 30, screenArea.top + 30, 30, 30, RGB(0, 0, 255));

	CString info;
	static int cycle = 1;
	info.Format(_T("L=%d,T=%d,R=%d,B=%d,T=%d"), screenArea.left, screenArea.top, screenArea.right, screenArea.bottom, cycle++);

	CFont *oldFont= screenDc.SelectObject(&labelFont);
	DWORD oldBkMode = screenDc.SetBkMode(TRANSPARENT);
	COLORREF oldTextColor = screenDc.SetTextColor(RGB(0, 0, 0));

	RECT infoRect;
	infoRect.left = screenArea.left + 40;
	infoRect.top = screenArea.top + 40;
	infoRect.right = screenArea.right;
	infoRect.bottom = screenArea.bottom;
	screenDc.DrawText(info, &infoRect, 0);

	screenDc.SetBkMode(oldBkMode);
	screenDc.SetTextColor(oldTextColor);
	screenDc.SelectObject(oldFont);

	bufferDc.SelectObject(oldBitmap);
}

#define GUTTER 10
#define HEIGHT 50

void StatusDlg::Paint(CDC& dc, RECT& area)
{
	dc.FillSolidRect(&area, RGB(255, 255, 255));

	CFont* oldFont = dc.SelectObject(&labelFont);
	DWORD oldBkMode = dc.SetBkMode(TRANSPARENT);
	COLORREF oldTextColor = dc.SetTextColor(RGB(0, 0, 0));

	CGdiObject* oldBrush = dc.SelectStockObject(NULL_BRUSH);
	CGdiObject* oldPen = dc.SelectStockObject(BLACK_PEN);

	COLORREF goodColor = RGB(96, 255, 96);
	COLORREF warningColor = RGB(255, 142, 96);
	COLORREF badColor = RGB(255, 96, 96);
	COLORREF naColor = RGB(128, 128, 128);

	COLORREF color = RGB(0, 0, 0);
	CString label;

	RECT indNetwork;
	indNetwork.left = area.left + GUTTER;
	indNetwork.right = area.right - GUTTER;
	indNetwork.bottom = area.bottom - GUTTER;
	indNetwork.top = indNetwork.bottom - GUTTER - HEIGHT;
	
	/*
	 * Originally this just said "Network Connection", but after talking
	 * to a few users it seems "Network" just means "Magic" for most users,
	 * but they do understand the words Ethernet and Wifi.
	 *
	 * So yes this is a little imprecise since there are other forms of
	 * networking.  Built-in cell comes to mind but I haven't run into
	 * those in the wild outside of high-end iPads.
	 *
	 * -DAW 201201
	 */
	if ((state == AVS_UNKNOWN) || (state == AVS_DISCONNECTED)) {
		label = _T("Ethernet or Wifi Connection");
	} else if (ssid.IsEmpty()) {
		label = _T("Ethernet Connection");
	} else {
		label = ssid;

		if (true || wifiProblem) {
			// Overlay strength and speed

			CString rateInfo;
			rateInfo.Format(_T(" %d/100"), signalQuality);
			label.Append(rateInfo);

			if (rxRate == txRate) {
				rateInfo.Format(
					_T(" %d Mbps"),
					rxRate / 1000);

				label.Append(rateInfo);
			} else {
				// Does this ever happen?
				rateInfo.Format(
					_T(" %d Mbps Down / %d Mbps Up"),
					rxRate / 1000, txRate / 1000);

				label.Append(rateInfo);
			}
		}
	}

	switch (state) {
	case AVS_UNKNOWN:
		color = naColor;
		break;

	case AVS_DISCONNECTED:
		color = badColor;
		break;

	case AVS_NETWORK:
	case AVS_INTRANET:
	case AVS_INTERNET:
	case AVS_VPN_DISABLED:
	case AVS_VPN_ENABLED:
	case AVS_VPN_CONNECTED:
		if (wifiProblem) {
			color = warningColor;
		} else {
			color = goodColor;
		}
		break;
	}

	dc.FillSolidRect(&indNetwork, color);
	dc.Rectangle(&indNetwork);
	dc.DrawText(label, &indNetwork, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	RECT indIntranet;
	indIntranet.left = area.left + GUTTER;
	indIntranet.bottom = indNetwork.top - GUTTER;
	indIntranet.top = indIntranet.bottom - (HEIGHT + HEIGHT + HEIGHT + GUTTER + GUTTER + GUTTER + GUTTER + GUTTER);
	indIntranet.right = area.left + ((area.right - area.left - GUTTER) >> 1);

	label = _T("On-Site Connection");

	switch (state) {
	case AVS_UNKNOWN:
	case AVS_DISCONNECTED:
	case AVS_NETWORK:
		color = naColor;
		break;
	case AVS_INTRANET:
		color = goodColor;
		break;
	case AVS_INTERNET:
	case AVS_VPN_ENABLED:
	case AVS_VPN_CONNECTED:
	case AVS_VPN_DISABLED:
		color = naColor;
		break;
	}
	dc.FillSolidRect(&indIntranet, color);
	dc.Rectangle(&indIntranet);
	dc.DrawText(label, &indIntranet, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	RECT indInternet;
	indInternet.right = area.right - GUTTER;
	indInternet.bottom = indNetwork.top - GUTTER;
	indInternet.top = indInternet.bottom - GUTTER - HEIGHT;
	indInternet.left = area.right - ((area.right - area.left - GUTTER) >> 1);

	label = _T("Internet Connection");
	switch (state) {
	case AVS_NETWORK:
		color = badColor;
		break;

	case AVS_UNKNOWN:
	case AVS_DISCONNECTED:
	case AVS_INTRANET:
		color = naColor;
		break;

	case AVS_INTERNET:
	case AVS_VPN_ENABLED:
	case AVS_VPN_CONNECTED:
	case AVS_VPN_DISABLED:
		color = goodColor;
		break;
	}
	dc.FillSolidRect(&indInternet, color);
	dc.Rectangle(&indInternet);
	dc.DrawText(label, &indInternet, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	RECT indVpnEnabled;
	indVpnEnabled.left = indInternet.left;
	indVpnEnabled.right = indInternet.right;
	indVpnEnabled.bottom = indInternet.top - GUTTER;
	indVpnEnabled.top = indVpnEnabled.bottom - GUTTER - HEIGHT;

	label = _T("VPN Client");
	switch (state) {
	case AVS_UNKNOWN:
	case AVS_DISCONNECTED:
	case AVS_NETWORK:
	case AVS_INTRANET:
		color = naColor;
		break;

	case AVS_INTERNET:
		color = badColor;
		break;

	case AVS_VPN_ENABLED:
	case AVS_VPN_CONNECTED:
		color = goodColor;
		break;

	case AVS_VPN_DISABLED:
		color = warningColor;
		label = _T("VPN Disabled");
		break;
	}
	dc.FillSolidRect(&indVpnEnabled, color);
	dc.Rectangle(&indVpnEnabled);
	dc.DrawText(label, &indVpnEnabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	RECT indVpnConnected;
	indVpnConnected.left = indVpnEnabled.left;
	indVpnConnected.right = indVpnEnabled.right;
	indVpnConnected.bottom = indVpnEnabled.top - GUTTER;
	indVpnConnected.top = indVpnConnected.bottom - GUTTER - HEIGHT;

	label = _T("VPN Connection");
	switch (state) {
	case AVS_UNKNOWN:
	case AVS_DISCONNECTED:
	case AVS_NETWORK:
	case AVS_INTRANET:
	case AVS_INTERNET:
		color = naColor;
		break;
	case AVS_VPN_ENABLED:
		color = badColor;
		break;
	case AVS_VPN_CONNECTED:
		color = goodColor;
		break;
	}
	dc.FillSolidRect(&indVpnConnected, color);
	dc.Rectangle(&indVpnConnected);
	dc.DrawText(label, &indVpnConnected, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	if (!suggestion.IsEmpty()) {
		dc.SetTextColor(RGB(255, 0, 0));
		dc.SelectObject(suggestionFont);

		RECT suggestionRect;
		suggestionRect.left = area.left + (GUTTER << 1);
		suggestionRect.right = area.right - (GUTTER << 1);
		suggestionRect.top = area.top + (GUTTER << 1);
		suggestionRect.bottom = indVpnConnected.top - (GUTTER << 1);

		dc.DrawText(suggestion, &suggestionRect, DT_WORDBREAK | DT_LEFT);
	}

	// Put the DC back like it was
	dc.SelectObject(oldBrush);
	dc.SelectObject(oldPen);
	dc.SetBkMode(oldBkMode);
	dc.SetTextColor(oldTextColor);
	dc.SelectObject(oldFont);
}

BOOL StatusDlg::OnCopyData(CWnd* pWnd, COPYDATASTRUCT* cds)
{
	// All updates and whatnot have to come in via WM_COPYDATA if they
	// change anything related to the UI.  Or in other words, you
	// can only do UI stuff on the UI thread.

	bool refreshScreen = false;
	bool attention = false;
	bool hideOnSuccess = false;

	if (cds->dwData == ServiceConnection::CDS_CONN_DATA) {
		if (cds->cbData >= sizeof(AutoVPNHeader)) {
			AutoVPNHeader* header = (AutoVPNHeader*)cds->lpData;
			if (header->version == AV_VERSION) {
				switch (header->opcode) {
				case AV_MESSAGE_STATUS:
					if (header->length == sizeof(AutoVPNStatus)) {
						if (cds->cbData >= (sizeof(AutoVPNHeader) + sizeof(AutoVPNStatus))) {
							AutoVPNStatus* data =
								(AutoVPNStatus*)((LPBYTE)cds->lpData + sizeof(AutoVPNHeader));

							bool newWifiProblem = (data->wifiProblem > 0);

							attention = false;
							if ((state != data->state) || (newWifiProblem != wifiProblem)) {
								attention = true;
							}

							if (visible) {
								// If we're currently visible, AND we're going from NOT in one of our
								// "final target" states TO one of our "final target" states, then
								// "everything is fine" and we should automatically get out of the
								// user's face.
								if ((state != AVS_VPN_CONNECTED) && (state != AVS_INTRANET)) {
									if ((data->state == AVS_VPN_CONNECTED) || (data->state == AVS_INTRANET)) {
										hideOnSuccess = true;
									}
								}
							}

							state = data->state;
							signalQuality = data->signalQuality;
							txRate = data->txRate;
							rxRate = data->rxRate;
							ssid = data->ssid;
							wifiProblem = newWifiProblem;

							refreshScreen = true;
						}
					}
					break;

				case AV_MESSAGE_SUGGESTION:
					// Well this is quite a dance.
					LPCTSTR stringData = (LPCTSTR)&((char *)cds->lpData)[sizeof(AutoVPNHeader)];
					size_t stringLen = header->length / sizeof(TCHAR);

					// Make sure it's null terminated
					if (stringData[stringLen] == '\0') {
						CString newSuggestion(stringData);

						if (newSuggestion.Compare(suggestion) != 0) {
							suggestion = newSuggestion;
							refreshScreen = true;

							if (!suggestion.IsEmpty()) {
								attention = true;
							}
						}
					}

					break;
				}
			}
		}
	} else if (cds->dwData == ServiceConnection::CDS_CONN_OFFLINE) {
		// We get this when we lose our connection to the service, meaning
		// we don't really know anything.  So just grey out everything.

		state = AVS_UNKNOWN;
		rxRate = 0;
		txRate = 0;
		ssid.Empty();
		suggestion.Empty();

		refreshScreen = true;
	} else if (cds->dwData == CDS_RELAUNCH) {
		// This is the user kicking us by trying to start a second copy.

		attention = true;
	}

	if (attention) {
		if (!visible) {
			visible = true;
			ShowWindow(SW_SHOW);

			// BringWindowToTop won't work immediately because the window hasn't been
			// drawn yet.  The solution is to put a 100ms timer in the queue and do the
			// BringWindowToTop then
			if (bringToFrontTimer == 0) {
				bringToFrontTimer = SetTimer(TIMERID_BRINGTOFRONT, 100, NULL);
			}
		} else {
			// There's some odd vudu around what it takes to force a window to the
			// front, but this seems to be working.
			BringWindowToTop();
			SetForegroundWindow();
		}
	}

	if (hideOnSuccess) {
		if (hideOnSuccessTimer == 0) {
			// 2 seconds seems to be enough for the user to see, but move quickly
			// out of the way of whatever they're trying to do.
			hideOnSuccessTimer = SetTimer(TIMERID_HIDEONSUCCESS, 2000, NULL);
		}
	}

	if (refreshScreen) {
		CDC* dc = GetDC();

		RECT clientRect;
		GetClientRect(&clientRect);

		int newHeight = (HEIGHT * 4) + (GUTTER * 9);
		if (!suggestion.IsEmpty()) {
			// Figure out how much height is needed for the message
			CFont* oldFont = dc->SelectObject(&suggestionFont);

			RECT suggestionRect;
			suggestionRect.left = clientRect.left + (GUTTER << 1);
			suggestionRect.right = clientRect.right - (GUTTER << 1);
			suggestionRect.top = clientRect.top + (GUTTER << 1);
			suggestionRect.bottom = clientRect.top + 1000;

			dc->DrawText(suggestion, &suggestionRect, DT_WORDBREAK | DT_LEFT | DT_CALCRECT);

			dc->SelectObject(oldFont);

			// I'm not sure why this 40 is needed but whatever
			newHeight += (suggestionRect.bottom - suggestionRect.top) + 40;
		}

		RECT windowRect;
		GetWindowRect(&windowRect);

		int decorationHeight =
			(windowRect.bottom - windowRect.top) -
			(clientRect.bottom - clientRect.top);

		windowRect.bottom = windowRect.top + newHeight + decorationHeight;

		// False because we're about to repaint it ourselves
		MoveWindow(&windowRect, FALSE);

		// This will have changed
		GetClientRect(&clientRect);

		Paint(*dc, clientRect);

		/*
		 * If you use the window DC instead of a PaintDC you have to release the DC
		 * when you're done or you get a GDI leak, which will make the UI croak
		 * after a few days runtime.  -DAW 201121
		 *
		 * From the docs:
		 *
		 * Unless the device context belongs to a window class, the ReleaseDC member
		 * function must be called to release the context after painting.
		 *
		 * A device context belonging to the CWnd class is returned by the GetDC member
		 * function if CS_CLASSDC, CS_OWNDC, or CS_PARENTDC was specified as a style
		 * in the WNDCLASS structure when the class was registered.
		 */
		ReleaseDC(dc);
	}

	return TRUE;
}

BOOL StatusDlg::OnEraseBkgnd(CDC* pDC)
{
	// The default erase paints over everything and makes the flashing worse.
	return TRUE;
}

BOOL StatusDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Set the icon for task bar
	HICON icon = ::AfxGetApp()->LoadIconW(IDI_DIALOG);
	SetIcon(icon, FALSE);
	SetIcon(icon, TRUE);

	// Create fonts we're going to use.  For some reason I think this is
	// a pretty heavyweight operation.  -DAW 201201
	labelFont.CreateFont(
		20,	// size
		0,
		0,
		0,
		FW_BOLD,
		FALSE,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY,
		DEFAULT_PITCH,
		_T("Arial"));

	suggestionFont.CreateFont(
		36,	// size
		0,
		0,
		0,
		FW_BOLD,
		FALSE,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY,
		DEFAULT_PITCH,
		_T("Arial"));

	// Start the connection to the service.
	service = new ServiceConnection();
	service->setCopyDataDest(m_hWnd);
	service->start();

	return TRUE;
}

void StatusDlg::OnTimer(UINT_PTR timerId)
{
	switch (timerId) {
	case TIMERID_BRINGTOFRONT:
		KillTimer(bringToFrontTimer);
		bringToFrontTimer = 0;

		BringWindowToTop();
		SetForegroundWindow();
		break;

	case TIMERID_HIDEONSUCCESS:
		KillTimer(hideOnSuccessTimer);
		hideOnSuccessTimer = 0;

		// If A) we're still visible because the user hasn't clicked us gone
		// already, and B) we're still in a "happy place", then close the
		// window.
		if (visible) {
			if ((state == AVS_INTRANET) || (state == AVS_VPN_CONNECTED)) {
				ShowWindow(SW_HIDE);
				visible = false;
			}
		}
		break;

	default:
		break;
	}
}
