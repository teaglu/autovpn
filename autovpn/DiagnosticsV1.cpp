/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#include "pch.h"
#include "Log.h"
#include "Message.h"
#include "Diagnostics.h"
#include "DiagnosticsV1.h"
#include "VerifyUrl.h"
#include "Settings.h"

DiagnosticsV1::DiagnosticsV1()
{
}

DiagnosticsV1::~DiagnosticsV1()
{
}

void DiagnosticsV1::diagnose(CallReason reason, AutoVPNStatus& status, CString& suggestion)
{
	switch (reason) {
	case CallReason::VPN_NOT_CONNECTING:
		diagnoseVpnNotConnecting(status, suggestion);
		break;
	default:
		break;
	}
}

void DiagnosticsV1::diagnoseVpnNotConnecting(AutoVPNStatus& status, CString &suggestion)
{
	CString unencryptedInternetUrl;
	CString unencryptedInternetContent;
	CString encryptedInternetUrl;
	CString encryptedInternetContent;

	Settings settings;

	// For the unencrypted there's no reason not to use the MS NCSI server
	if (!settings.readString(_T("UnencryptedInternetUrl"), unencryptedInternetUrl)) {
		unencryptedInternetUrl = _T("http://www.msftncsi.com/ncsi.txt");
	}
	if (!settings.readString(_T("UnencryptedInternetContent"), unencryptedInternetContent)) {
		unencryptedInternetContent = _T("Microsoft NCSI");
	}

	settings.readString(_T("EncryptedInternetUrl"), encryptedInternetUrl);
	settings.readString(_T("EncryptedInternetContent"), encryptedInternetContent);

	if (!unencryptedInternetUrl.IsEmpty()) {
		// This is more or less a basic NCSI check.  If we get some weird content back
		// then we're probably behind a captive portal.  If we get some other error then
		// we're not connected to the internet.
		VerifyUrl::Status unencryptedStatus =
			VerifyUrl::verifyUrl(unencryptedInternetUrl, unencryptedInternetContent);

		if (unencryptedStatus == VerifyUrl::Status::ERR_WRONG_CONTENT) {
			status.state = AVS_NETWORK;
			suggestion = _T("V1_NSCI_INTERCEPT");

		} else if (unencryptedStatus != VerifyUrl::Status::SUCCESS) {
			status.state = AVS_NETWORK;
			suggestion = _T("V1_NCSI_FAILURE");
		} else {
			if (!encryptedInternetUrl.IsEmpty()) {
				// If the unencrypted check passes but we're not connecting, then run a
				// check to an encrypted endpoint.  This should flush out man-in-the-middle
				// firewalls aka "SSL inspection".

				// Normally people should run into such a thing unless they're on a
				// wifi they shouldn't be on or god forbid just jacked into an ethernet
				// plug at J Random Company.  People do stuff though.

				// NOTE: This shouldn't actually catch a MITM at your own company as long
				// as you push it out via GPO, since WinHttp uses the Windows certificate
				// store.

				VerifyUrl::Status encryptedStatus =
					VerifyUrl::verifyUrl(encryptedInternetUrl, encryptedInternetContent);

				if (encryptedStatus != VerifyUrl::Status::SUCCESS) {
					status.state = AVS_NETWORK;

					switch (encryptedStatus) {
					case VerifyUrl::Status::ERR_SECURITY_FAILED:
					case VerifyUrl::Status::ERR_WRONG_CONTENT:
						suggestion = _T("V1_ENCSI_INTERCEPT");
						break;

					default:
						suggestion = _T("V1_ENCSI_FAILURE");
						break;
					}
				}
			}
		}
	}
}
