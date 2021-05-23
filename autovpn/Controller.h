/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#pragma once

#include "Message.h"

class Ip4Network;
class DiagnosticsV1;
class Settings;
class Controller
{
public:
	class StatusListener {
	public:
		virtual void onStatusChanged(AutoVPNStatus *) = 0;
		virtual void onSuggestion(LPCTSTR) = 0;
	};

	Controller();
	virtual ~Controller();

	void main();
	void stop();

	void registerStatusListener(StatusListener*);
	void unregisterStatusListener(StatusListener*);

private:
	mutex lock;
	volatile bool run;
	condition_variable wake;

	void cycle();

	AutoVPNStatus status;
	list<StatusListener*> statusListeners;
	DiagnosticsV1 *diagnostics;

	bool checkEnabled(Settings& settings);
	void loadInternalNetworks(Settings &, list<shared_ptr<Ip4Network>>&);
	void loadAttachedNetworks(list<shared_ptr<Ip4Network>>&, bool &foundEthernet, bool &foundWifi, bool &foundVpnAdapter);
	void getWifiInfo(AutoVPNStatus& status);
};
