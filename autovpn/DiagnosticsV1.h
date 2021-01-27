/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#pragma once
class DiagnosticsV1 : public Diagnostics {
public:
	DiagnosticsV1();
	virtual ~DiagnosticsV1();

	virtual void diagnose(CallReason reason, AutoVPNStatus& status, CString &suggestion);
private:

	void diagnoseVpnNotConnecting(AutoVPNStatus& status, CString& suggestion);
};
