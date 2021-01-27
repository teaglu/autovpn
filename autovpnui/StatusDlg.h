/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 ******************************************************************************/

#pragma once


class ServiceConnection;

class StatusDlg : public CDialogEx
{
	DECLARE_DYNAMIC(StatusDlg)

public:
	StatusDlg(CWnd* pParent = nullptr);   // standard constructor
	virtual ~StatusDlg();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_STATUS };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	void BufferedPaint(CDC&, RECT &);
	void Paint(CDC&, RECT &);

	CFont labelFont;
	CFont suggestionFont;

	ServiceConnection* service;

	short state;
	unsigned short signalQuality;
	unsigned long txRate;
	unsigned long rxRate;
	CString ssid;

	bool wifiProblem;
	bool visible;

	CString suggestion;

	UINT_PTR bringToFrontTimer;
	UINT_PTR hideOnSuccessTimer;

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnDialogOk();
	afx_msg void OnDialogCancel();
	virtual void PostNcDestroy();
	afx_msg void OnPaint();
	afx_msg BOOL OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	virtual BOOL OnInitDialog();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
};
