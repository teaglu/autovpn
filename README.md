# AutoVPN

## Purpose

This project is designed to:

1. Detect whether the computer is currently connected to the corporate network, and start or stop a VPN service as needed.

2. If there is a problem connecting to the corporate network, perform automatic diagnostics and present a suggested action to the end user.

## Installation

You are more than welcome to compile your own version, or you can use the [OV-Signed installation packages](https://github.com/teaglu/autovpn/releases).

The main portion of the program runs as a service, so the user does not need to have administrative priviliges.

## Configuration

The program does not have a configuration utility - it is configured using registry entries which are expected to be pushed out via group policy.  Configuration is done first by checking for policy keys under HKEY_LOCAL_MACHINE\Software\Policies\Teaglu\AutoVPN, then checking for preference keys under HKEY_LOCAL_MACHINE\Software\Teaglu\AutoVPN.

The included ADMX files can be used for configuration, or the following registry keys can be set:

### VPNServiceName

The service which should be started and stopped to activate and de-activate the VPN software.  The default is OpenVPNService which corresponds to the open source OpenVPN client.  If you use WireGuard instead, use this value to match the name of the service you create using "wireguard /installservice".

### InternalNetworks

This is a registry key containing IP subnets corresponding to your corporate network.  The names of the keys are not used.  Each TEXT value under this key is parsed as a comma-separated list of subnets in x.x.x.x/n or x.x.x.x/y.y.y.y format.  Normally you would use a single entry, but it is possible to use multiple entries so that multiple group policy can combine additively.

The subnet does not have to exactly match what is present on the computer's interface - it can be a larger subnet that includes the subnet which appears on the computer's interface.  This roughly corresponds to an "orlonger" match.

### WifiSignalWarningLimit - DWORD

This is a value between 0 to 100 indicating the signal strength, where 0 corresponds to -100dbm and 100 corresponds to -50dbm.  If the signal strength of the active association falls below this value a warning is shown to the user.  The value can be linearly interpolated between the two, that is the value follows the logarithmic meaning of dbm.

This value corresponds to the wlanSignalQuality of the WLAN_ASSOCIATION_INFO structure detailed at [this page](https://docs.microsoft.com/en-us/windows/win32/api/wlanapi/ns-wlanapi-wlan_association_attributes).

### WifiRxRateWarningLimit	- DWORD

This is a value in kilobits/second.  If the receive rate falls below this limit, a warning is shown to the user.

### WifiTxRateWarningLimit - DWORD

### UnencryptedInternetUrl - TEXT

This value is a URL which is retrieved as part of diagnostics, to determine internet connectivity as well as determine if there is a captive portal in the way.  This serves the same function as Microsoft NCSI, and if this key is not present the value "http://www.msftncsi.com/ncsi.txt" is used.

### UnencryptedInternetContent - TEXT

This value is compared to the result of retrieving the unencrypted internet URL to determine if a captive portal is in place.  If this key is not present the value "Microsoft NCSI" is used.

### EncryptedInternetUrl - TEXT

If the unencrypted internet URL is retrieved and shows no issues, this URL is retrieved to determine the usability of HTTPS connections.  If the connection fails with a security or TLS failure, it is assumed that there is an SSL Inspection firewall in the way which will most likely prohibit encrypted connections.  There is no default value for this key, because I don't want to DDOS myself.

Note that if you have a root certificate installed for *your* SSL Inspection firewall, this check will not fail because the WinHttp library is used for the check, and that library uses the windows certificate store.

### EncryptedInternetContent - TEXT

This value is compared to the result of the encrypted internet URL, to determine if content is being changed in transit.  This is effectively an encrypted form of NCSI.

### Messages - KEY

Values under this key translate warning messages from the tag value generated by the code to the text shown to the user as a warning.  Look at the existing keys in the installation package for definitions.  This can be used if you need to alter the displayed text for clarity or legal reasons.

## To-Do

1. Adjust default signal warning limits based on community feedback.

2. Internationalization

3. Log to the event log instead of a separate file.
