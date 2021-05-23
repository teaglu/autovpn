/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#include "pch.h"
#include "Controller.h"
#include "Log.h"
#include "Settings.h"
#include "Ip4Network.h"
#include "SessionManager.h"
#include "Log.h"
#include "Diagnostics.h"
#include "DiagnosticsV1.h"

#define CYCLE_SECONDS 5

// DEBUG_MEMORY makes the process shut down after a finite number
// of main loop cycles - that way there's a normal shutdown and the normal
// memory debugging stuff can check for leaks

//#define DEBUG_MEMORY 1

#ifdef DEBUG_MEMORY
#ifndef _DEBUG
#error "Don't leave DEBUG_MEMORY set in release builds, or service would continually stop itself"
#endif
#endif

Controller::Controller()
{
	ZeroMemory(&status, sizeof(status));
	run = true;
	diagnostics = new DiagnosticsV1();
}

Controller::~Controller()
{
	delete diagnostics;
}

void Controller::main()
{
	SessionManager* sessionManager = new SessionManager(this);
	sessionManager->start();

#ifdef DEBUG_MEMORY
	bool localRun = true;
	for (int cycleCnt= 0; localRun && (cycleCnt < 10); cycleCnt++) {
#else
	for (bool localRun = true; localRun; ) {
#endif
		cycle();

		{
			unique_lock<mutex> permit(lock);
			localRun = run;
			if (localRun) {
				wake.wait_for(permit, chrono::seconds(CYCLE_SECONDS));
				localRun = run;
			}
		}
	}

	sessionManager->stop();
	delete sessionManager;
}

void Controller::stop()
{
	unique_lock<mutex> permit(lock);
	run = false;
	wake.notify_all();
}

bool Controller::checkEnabled(Settings& settings)
{
	bool enabled = true;

	CString enableHostname;
	settings.readString(_T("EnableHostname"), enableHostname);

	if (!enableHostname.IsEmpty()) {
		ADDRINFOEXW hints;

		// Winsock is picky about several members being 0 or null, this isn't
		// just cargo cult - it's in the docs
		ZeroMemory(&hints, sizeof(ADDRINFOEXW));

		// Only resolve if we have an actual address - this should save pointless
		// queries if we're not on the network.
		hints.ai_flags = AI_ADDRCONFIG;

		// Our flags are A (IPv4) records, so that's all we care about
		hints.ai_family = AF_INET;

		// This doesn't really matter, but a VPN is more likely than not to be UDP
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;

		PADDRINFOEXW results= NULL;

		INT status = GetAddrInfoEx(enableHostname,
			NULL, NS_DNS, NULL, &hints, &results, NULL, NULL, NULL, NULL);

		// I can't find it actually stated in the docs that if you get NO_RESULT the
		// result has to be filled in.  I don't know why it wouldn't though.
		if ((status == NO_ERROR) && (results != NULL)) {
			if (results->ai_addrlen > 0) {
				// Every version of sockaddr has family in the first member, so this
				// is safe as long as we check the family first
				struct sockaddr_in* found4 =
					reinterpret_cast<struct sockaddr_in*>
					(&results->ai_addr[0]);

				// We only asked for IPV4, but be defensive
				if (found4->sin_family == AF_INET) {
					unsigned long rawIp = ntohl(found4->sin_addr.S_un.S_addr);

					// Use 127.0.0.2 to mean enabled and 127.0.0.3 to mean disabled.
					//
					// 1) I wanted to use A records because it's the most widely-implemented
					//    type - most web providers these days implement TXT at least, but
					//    A records are a lowest common denominator.
					//
					// 2) We can't use existence or non-existence because some ISPs
					//    (as well as OpenDNS) will return a server of theirs instead
					//    of NXDOMAIN.  So we have to use something that can't normally
					//    occur on the open internet.
					//
					// 3) It used to be common practice to put localhost in zone files, or
					//    I used to see it done.  So I'm avoiding 127.0.0.1.
					//
					// 4) AFAIK the alternate loopback addresses aren't in common use.  I
					//    think I've seen them used for proxies with some ASA web portal
					//    stuff, but I can't think of anywhere else.
					//
					// 5) There's precident in how some DNSRBLs are implemented.
					//
					if (rawIp == 0x7F000002) {
						enabled = true;
					} else if (rawIp == 0x7F000003) {
						enabled = false;
					} else {
						// This is DEBUG because we don't want to spam the log if we're
						// behind something that won't return NXDOMAIN

						Log::log(LOG_DEBUG,
							_T("Enable hostname value 0x08X not understood"),
							rawIp);
					}
				}
			}

			FreeAddrInfoEx(results);
			results = NULL;
		} else {
			if (status != WSAHOST_NOT_FOUND) {
				Log::log(LOG_WARNING,
					_T("Unable to query for disabled hostname: %d"), status);
			}
		}
	}

	return enabled;
}

void Controller::cycle()
{
	// I realize that this pulls the settings from the registry every cycle which isn't
	// the most optimized thing in the world.  This is intentional so that if the registry
	// is changed via GPO updates we pick it up right away.

	CString vpnServiceName(_T("OpenVPNService"));

	int signalWarningLimit = 50;
	int rxRateWarningLimit = 10000;
	int txRateWarningLimit = 10000;

	Settings settings;
	settings.readString(_T("VPNServiceName"), vpnServiceName);

	settings.readInt(_T("WifiSignalWarningLimit"), signalWarningLimit);
	settings.readInt(_T("WifiRxRateWarningLimit"), rxRateWarningLimit);
	settings.readInt(_T("WifiTxRateWarningLimit"), txRateWarningLimit);

	AutoVPNStatus oldStatus;
	{
		unique_lock<mutex> permit(lock);
		oldStatus = status;
	}

	AutoVPNStatus newStatus;
	ZeroMemory(&newStatus, sizeof(AutoVPNStatus));

	newStatus.state = AVS_DISCONNECTED;

	list<shared_ptr<Ip4Network>> attachedList;
	bool foundEthernet = false;
	bool foundWifi = false;
	bool foundVpn = false;
	loadAttachedNetworks(attachedList, foundEthernet, foundWifi, foundVpn);

	bool onAnyNetwork = !attachedList.empty();

	bool vpnShouldBeRunning = false;
	CString suggestion;

	if (onAnyNetwork) {
		newStatus.state = AVS_NETWORK;

		list<shared_ptr<Ip4Network>> internalList;
		loadInternalNetworks(settings, internalList);
		bool onInternalNetwork = false;

		for (shared_ptr<Ip4Network> internal: internalList) {
			for (shared_ptr<Ip4Network> attached : attachedList) {
				if (internal->includes(attached.get())) {
					onInternalNetwork = true;
				}
			}
		}

		if (!onInternalNetwork) {
			// For now we assume we have internet connectivity.  Later on if the VPN isn't
			// connected we run an HTTP check to make sure we can actually get to the -
			// that way the user indication will make more sense.

			if (!checkEnabled(settings)) {
				newStatus.state = AVS_VPN_DISABLED;
			} else {
				newStatus.state = AVS_INTERNET;
				vpnShouldBeRunning = true;
			}
		} else {
			newStatus.state = AVS_INTRANET;
		}

		if (!foundEthernet && foundWifi) {
			// This is making the assumption that the PC will use wired networking over wireless,
			// and I don't 100% know that's true.  We don't actually do anything with the status,
			// but we do it on the service side to avoid permission issues on locked-down
			// laptops (not sure if that's a thing).
			//
			// The user side uses the presense or absence of an SSID to change the indicator
			// to Wifi, and to look at the RX/TX speeds to warn on that.  The idea is we don't
			// want people calling help lines about slowness when they have a 2Mb uplink
			// because they're by the pool 300 feet from the hub.
			getWifiInfo(newStatus);

			if (newStatus.ssid[0] != '\0') {
				if (newStatus.signalQuality < signalWarningLimit) {
					newStatus.wifiProblem = 1;
					suggestion = _T("WIFI_SIGNAL_LOW");
				} else if (newStatus.rxRate < (unsigned long)rxRateWarningLimit) {
					newStatus.wifiProblem = 1;
					suggestion = _T("WIFI_RATE_LOW");
				} else if (newStatus.txRate < (unsigned long)txRateWarningLimit) {
					newStatus.wifiProblem = 1;
					suggestion = _T("WIFI_RATE_LOW");
				}
			}
		}
	}

	bool vpnIsRunning = false;

	SC_HANDLE serviceManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	if (serviceManager == NULL) {
		Log::log(LOG_ERROR, _T("can't open service control manager: {w32err}"));
	} else {
		SC_HANDLE vpnService = OpenService(serviceManager,
			vpnServiceName, SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);

		if (vpnService == NULL) {
			Log::log(LOG_ERROR, _T("Unable to open VPN service: {w32err}"));
		} else {
			SERVICE_STATUS vpnStatus;
			ZeroMemory(&vpnStatus, sizeof(SERVICE_STATUS));
			if (!QueryServiceStatus(vpnService, &vpnStatus)) {
				Log::log(LOG_ERROR,
					_T("Unable to query status of VPN service: {w32err}"));
			} else {
				switch (vpnStatus.dwCurrentState) {
				case SERVICE_RUNNING:
					if (!vpnShouldBeRunning) {
						Log::log(LOG_INFO, _T("Stopping VPN Service"));
						if (!ControlService(vpnService, SERVICE_CONTROL_STOP, &vpnStatus)) {
							Log::log(LOG_ERROR,
								_T("Unable to stop VPN service: {w32err}"));

							vpnIsRunning = true;
						}
					} else {
						vpnIsRunning = true;
					}
					break;

				case SERVICE_STOPPED:
					if (vpnShouldBeRunning) {
						Log::log(LOG_INFO, _T("Starting VPN Service"));
						if (!StartService(vpnService, 0, NULL)) {
							Log::log(LOG_ERROR,
								_T("Failed to start VPN service: {w32err}"));
						} else {
							vpnIsRunning = true;
						}
					}
					break;

				default:
					Log::log(LOG_WARNING,
						_T("Unexpected VPN status 0x%08X"), vpnStatus.dwCurrentState);
				}
			}

			CloseServiceHandle(vpnService);
		}

		CloseServiceHandle(serviceManager);
	}

	if (vpnShouldBeRunning) {
		if (vpnIsRunning) {
			if (foundVpn) {
				newStatus.state = AVS_VPN_CONNECTED;
			} else {
				newStatus.state = AVS_VPN_ENABLED;
			}
		}
	}

	if (newStatus.state == AVS_VPN_ENABLED) {
		diagnostics->diagnose(
			DiagnosticsV1::CallReason::VPN_NOT_CONNECTING,
			newStatus, suggestion);
	}

	if (!suggestion.IsEmpty()) {
		Settings suggestions = settings.getSubKey(_T("Suggestions"));

		if (suggestions.readString(suggestion, suggestion)) {
			// Handle any insertion variables
			if (status.ssid[0] != '\0') {
				CA2T wideSsid(status.ssid);
				suggestion.Replace(_T("${SSID}"), wideSsid);
			} else {
				// This isn't entirely accurate, but from my initial demos
				// people use the word "network" to mean "magic" but understand
				// the word "ethernet".  The other common option would be
				// built-in WWAN cards and I don't have one of those to test
				// with.
				suggestion.Replace(_T("${SSID}"), _T("Ethernet"));
			}
		}
	}

	unique_lock<mutex> permit(lock);
	status = newStatus;

	for (StatusListener* listener : statusListeners) {
		listener->onStatusChanged(&newStatus);

		// We have to send the "empty suggestion" if that's the case, because otherwise
		// the UI will keep indicating the same problem forever.  The SessionConnection
		// will take care of not sending the same thing over and over.
		listener->onSuggestion(suggestion);
	}
}

void Controller::registerStatusListener(StatusListener* listener)
{
	AutoVPNStatus localStatus;

	{
		unique_lock<mutex> permit(lock);
		statusListeners.push_back(listener);
		localStatus = status;
	}

	listener->onStatusChanged(&localStatus);
}

void Controller::unregisterStatusListener(StatusListener* listener)
{
	unique_lock<mutex> permit(lock);
	statusListeners.remove(listener);
}

#define MAX_NETWORK_KEY_NAME 31
#define MAX_NETWORK_KEY_VALUE 127

void Controller::loadInternalNetworks(Settings &settings, list<shared_ptr<Ip4Network>>& trustedNetworks)
{
	for (int pass= 0; pass < 2; pass++) {
		HKEY root = (pass > 0) ? settings.getPreferenceRoot() : settings.getPolicyRoot();

		HKEY internalKey;
		DWORD regStatus= RegOpenKeyEx(root,
			_T("InternalNetworks"), 0, KEY_QUERY_VALUE, &internalKey);

		if (regStatus == ERROR_SUCCESS) {
			DWORD index = 0;
			while (true) {
				TCHAR name[MAX_NETWORK_KEY_NAME + 1];
				DWORD nameLen = MAX_NETWORK_KEY_NAME;
				TCHAR value[MAX_NETWORK_KEY_VALUE + 1];
				DWORD valueLen = MAX_NETWORK_KEY_VALUE * sizeof(TCHAR);

				DWORD regType = 0;

				regStatus = RegEnumValue(internalKey, index,
					name, &nameLen, NULL, &regType, (LPBYTE)value, &valueLen);

				if (regStatus == ERROR_NO_MORE_ITEMS) {
					break;
				} else if (regStatus == ERROR_SUCCESS) {
					value[valueLen / sizeof(TCHAR)] = '\0';

					CString splitBuf(value);

					LPCTSTR tokens = _T(" ,;");

					int tokenPos = 0;
					CString token = splitBuf.Tokenize(tokens, tokenPos);
					while (!token.IsEmpty()) {
						shared_ptr<Ip4Network> network = Ip4Network::Create(token);
						if (network) {
							trustedNetworks.push_back(network);
						} else {
							Log::log(LOG_DEBUG,
								_T("Unable to understand network %s"), (LPCTSTR)token);
						}

						token = splitBuf.Tokenize(tokens, tokenPos);
					}

					index++;
				} else {
					Log::log(LOG_ERROR,
						_T("Error enumeriting trusted networks: 0x%08X"),
						regStatus);
					break;
				}
			}

			RegCloseKey(internalKey);
		} else {
			Log::log(LOG_ERROR,
				_T("Error opening internal network registry key: 0x%08X"),
				regStatus);
		}
	}
}

void Controller::loadAttachedNetworks(list<shared_ptr<Ip4Network>>& attachedList, bool &foundEthernet, bool &foundWifi, bool &foundVpn)
{
	foundEthernet = false;
	foundWifi = false;
	foundVpn = false;

	DWORD bufferSize = sizeof(IP_ADAPTER_INFO);
	PIP_ADAPTER_INFO buffer = (PIP_ADAPTER_INFO)new BYTE[bufferSize];

	DWORD infoRval = GetAdaptersInfo(buffer, &bufferSize);
	if (infoRval == ERROR_BUFFER_OVERFLOW) {
		// Re-rez buffer with enough space
		delete[] buffer;
		buffer = (PIP_ADAPTER_INFO)new BYTE[bufferSize];

		infoRval = GetAdaptersInfo(buffer, &bufferSize);
	}

	if (infoRval == NO_ERROR) {
		for (PIP_ADAPTER_INFO curr = buffer; curr != NULL; curr = curr->Next) {
			bool foundGateway = false;  // This is just up here to avoid case initialization warnings

			switch (curr->Type) {
			case MIB_IF_TYPE_ETHERNET:
			case IF_TYPE_IEEE80211:
				// FIXME We might need to add WWAN cards here too, but I don't have any of those to double-check
				// what they show up as in practice.  -DAW

				for (PIP_ADDR_STRING ipEntry = &curr->GatewayList; ipEntry != NULL; ipEntry = ipEntry->Next) {
					if (strcmp(ipEntry->IpAddress.String, "0.0.0.0") != 0) {
						foundGateway = true;
					}
				}

				// We don't want to bother looking at anything without a gateway.  Weird stuff that's
				// networked but not really can show up as NICs, like connections to management cards
				// and bluetooth.  We also don't want psuedo-interfaces like VMware Workstation or
				// whatever the MS equivalent is.  Those should be filtered out by type but this makes
				// double-sure.
				if (foundGateway) {
					for (PIP_ADDR_STRING ipEntry = &curr->IpAddressList; ipEntry != NULL; ipEntry = ipEntry->Next) {
						if (strcmp(ipEntry->IpAddress.String, "0.0.0.0") != 0) {
							shared_ptr<Ip4Network> network = Ip4Network::Create(ipEntry->IpAddress.String, ipEntry->IpMask.String);
							if (network) {
								attachedList.push_back(network);

								if (curr->Type == MIB_IF_TYPE_ETHERNET) {
									foundEthernet = true;
								} else if (curr->Type == IF_TYPE_IEEE80211) {
									foundWifi = true;
								}
							}
						}
					}
				}
				break;

			case IF_TYPE_PROP_VIRTUAL:
				// Some adapters are always there, so check it has a valid IP4 address
				for (PIP_ADDR_STRING ipEntry = &curr->IpAddressList; ipEntry != NULL; ipEntry = ipEntry->Next) {
					if (strcmp(ipEntry->IpAddress.String, "0.0.0.0") != 0) {
						foundVpn = true;
					}
				}
				break;

			default:
				static set<int> reportedTypes;

				// Log these so we can see what's showing up in real life.  Just log it one time though,
				// so we don't spam the log with this.
				if (reportedTypes.find(curr->Type) == reportedTypes.end()) {
					reportedTypes.insert(curr->Type);

					CString name(curr->AdapterName);
					Log::log(LOG_WARNING,
						_T("Ignoring type %d adapter: %s - look up in ipifcons.h"),
						curr->Type, (LPCTSTR)name);
				}
			}
		}
	}
}

void Controller::getWifiInfo(AutoVPNStatus &status)
{
	DWORD negotiatedVersion = 0;
	HANDLE wifiHandle = INVALID_HANDLE_VALUE;

	DWORD errorStatus = WlanOpenHandle(2, NULL, &negotiatedVersion, &wifiHandle);
	if (errorStatus != ERROR_SUCCESS) {
		Log::log(LOG_ERROR,
			_T("Error from WlanOpenHandle: %08X"), errorStatus);
	} else {
		PWLAN_INTERFACE_INFO_LIST wifiList= NULL;

		errorStatus = WlanEnumInterfaces(wifiHandle, NULL, &wifiList);
		if (errorStatus != ERROR_SUCCESS) {
			Log::log(LOG_ERROR,
				_T("Error in WlanEnumInterfaces: %08X"), errorStatus);
		} else {
			for (DWORD i = 0; i < wifiList->dwNumberOfItems; i++) {
				WLAN_INTERFACE_INFO* info = &wifiList->InterfaceInfo[i];

				if (info->isState == wlan_interface_state_connected) {
					PVOID data;
					DWORD dataSize = 0;

					errorStatus = WlanQueryInterface(wifiHandle,
						&info->InterfaceGuid, wlan_intf_opcode_current_connection,
						NULL, &dataSize, &data, NULL);

					if (errorStatus == ERROR_SUCCESS) {
						WLAN_CONNECTION_ATTRIBUTES* attr = (WLAN_CONNECTION_ATTRIBUTES*)data;

						size_t ssidLength = attr->wlanAssociationAttributes.dot11Ssid.uSSIDLength;
						if (ssidLength > (sizeof(status.ssid) - 1)) {
							ssidLength = sizeof(status.ssid) - 1;
						}
						strncpy_s(status.ssid,
							(const char *)attr->wlanAssociationAttributes.dot11Ssid.ucSSID,
							ssidLength);
						status.ssid[ssidLength] = '\0';

						// This is a 0-100 thing - 0 = -100dbm, 100 = -50dbm
						status.signalQuality = (short)attr->wlanAssociationAttributes.wlanSignalQuality;

						status.rxRate = attr->wlanAssociationAttributes.ulRxRate;
						status.txRate = attr->wlanAssociationAttributes.ulTxRate;

						WlanFreeMemory(data);
					}
				}
			}

			WlanFreeMemory(wifiList);
		}
		WlanCloseHandle(wifiHandle, NULL);
	}
}
