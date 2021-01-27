/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#include "pch.h"
#include "Log.h"
#include "VerifyUrl.h"

#define READ_BUFFER_SIZE 128 // FIXME longer in prod

VerifyUrl::Status VerifyUrl::verifyUrl(LPCTSTR urlIn, LPCTSTR expectedIn)
{
	Status rval = Status::ERR_UNKNOWN;

	CString url(urlIn);

	CT2A expected(expectedIn);
	size_t expectedLen = strlen(expected.m_psz);

	DWORD winhttpError = 0;

	HINTERNET session = WinHttpOpen(
		_T("Teaglu AutoVPN Verifier"),
		WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);

	if (session == NULL) {
		Log::log(LOG_ERROR,
			_T("Unable to initialize WinHttp session: {w32err}"));

		winhttpError = GetLastError();
	} else {
		URL_COMPONENTS urlParts;
		ZeroMemory(&urlParts, sizeof(urlParts));
		urlParts.dwStructSize = sizeof(urlParts);
		urlParts.dwSchemeLength = -1;
		urlParts.dwHostNameLength = -1;
		urlParts.dwUrlPathLength = -1;

		if (!WinHttpCrackUrl(url.GetString(), url.GetLength(), 0, &urlParts)) {
			Log::log(LOG_ERROR,
				_T("Unable to parse URL with WinHttpCrackUrl: {w32err}"));
		} else {
			bool isSecure = (urlParts.nScheme == INTERNET_SCHEME_HTTPS);

			if (isSecure) {
				static unsigned long trySecurityProtocols[] = {
					WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2,
					WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
				};
				static int trySecurityProtocolsCnt = sizeof(trySecurityProtocols) / sizeof(trySecurityProtocols[0]);

				bool securityProtocolWorked = false;
				for (int i = 0; !securityProtocolWorked && (i < trySecurityProtocolsCnt); i++) {
					unsigned long secureProtocols = trySecurityProtocols[i];

					if (WinHttpSetOption(session,
						WINHTTP_OPTION_SECURE_PROTOCOLS,
						&secureProtocols, sizeof(secureProtocols))) {
						securityProtocolWorked = true;
					} else {
						Log::log(LOG_DEBUG,
							_T("Unable to set WINHTTP_OPTION_SECURE_PROTOCOLS: {w32err}"));
					}
				}

				if (!securityProtocolWorked) {
					Log::log(LOG_ERROR,
						_T("Unable to set TLS compatibility to an acceptable value"));
				}
			}

			LPWSTR hostname = new WCHAR[urlParts.dwHostNameLength + 1];
			StrCpyNW(hostname, urlParts.lpszHostName, urlParts.dwHostNameLength + 1);
			hostname[urlParts.dwHostNameLength] = '\0';

			LPWSTR urlPath = new WCHAR[urlParts.dwUrlPathLength + 1];
			StrCpyNW(urlPath, urlParts.lpszUrlPath, urlParts.dwUrlPathLength + 1);
			urlPath[urlParts.dwUrlPathLength] = '\0';

			HINTERNET connection = WinHttpConnect(
				session,
				hostname,
				urlParts.nPort,
				0);

			if (connection == NULL) {
				Log::log(LOG_ERROR,
					_T("Unable to create WinHttp connection: {w32err}"));
			} else {
				CA2W wideVerb("GET");

				DWORD requestFlags = WINHTTP_FLAG_BYPASS_PROXY_CACHE | WINHTTP_FLAG_REFRESH;
				if (isSecure) {
					requestFlags |= WINHTTP_FLAG_SECURE;
				}

				HINTERNET httpRequest = WinHttpOpenRequest(
					connection,
					wideVerb.m_psz,
					urlPath,
					NULL,
					WINHTTP_NO_REFERER,
					WINHTTP_DEFAULT_ACCEPT_TYPES,
					requestFlags);

				if (httpRequest == NULL) {
					winhttpError = GetLastError();
					Log::log(LOG_ERROR,
						_T("Unable to open WinHttp request: %d"), winhttpError);
				} else {
					if (!WinHttpSendRequest(httpRequest,
						WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, NULL)) {
						winhttpError = GetLastError();
						Log::log(LOG_ERROR,
							_T("Error sending HTTP request: %d"), winhttpError);
					} else {
						if (!WinHttpReceiveResponse(httpRequest, NULL)) {
							winhttpError = GetLastError();
							Log::log(LOG_ERROR,
								_T("Error in WinHttpReceiveResponse: %d"), winhttpError);
						} else {
							std::string response;

							DWORD status;
							DWORD statusSize = sizeof(status);
							if (!WinHttpQueryHeaders(
								httpRequest,
								WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
								WINHTTP_HEADER_NAME_BY_INDEX,
								&status,
								&statusSize,
								WINHTTP_NO_HEADER_INDEX)) {
								winhttpError = GetLastError();
								Log::log(LOG_ERROR,
									_T("Error gettings HTTP status code: %d"), winhttpError);

								status = 999;
							}

							if ((status >= 200) && (status <= 299)) {
								DWORD bufferLen;
								CHAR buffer[READ_BUFFER_SIZE];

								for (bool readRun = true; readRun; ) {
									if (!WinHttpQueryDataAvailable(httpRequest, &bufferLen)) {
										Log::log(LOG_ERROR,
											_T("Error in WinHttpQueryDataAvailable: {w32err}"));
										readRun = false;
									} else if (bufferLen == 0) {
										readRun = false;

										rval = Status::ERR_WRONG_CONTENT;
										if (response.size() >= expectedLen) {
											if (strncmp(response.c_str(), expected.m_psz, expectedLen) == 0) {
												rval = Status::SUCCESS;
											}
										}
									} else {
										bufferLen = READ_BUFFER_SIZE;
										if (!WinHttpReadData(httpRequest, buffer, READ_BUFFER_SIZE, &bufferLen)) {
											Log::log(LOG_ERROR,
												_T("Error reading from WinHttp: {w32err}"));
											readRun = false;
										} else {
											// If we don't have enough to compare add this chunk
											if (response.size() < expectedLen) {
												response.append(buffer, bufferLen);
											}
										}
									}
								}
							}
						}
					}
					WinHttpCloseHandle(httpRequest);
				}
				WinHttpCloseHandle(connection);
			}
			WinHttpCloseHandle(session);

			delete[] hostname;
			delete[] urlPath;
		}
	}

	if ((rval == Status::ERR_UNKNOWN) && (winhttpError > 0)) {
		// Try to decode normal errors to something more useful
		switch (winhttpError) {
		case ERROR_WINHTTP_NAME_NOT_RESOLVED:
			rval = Status::ERR_DNS_FAILED;
			break;

		case ERROR_WINHTTP_CONNECTION_ERROR:
		case ERROR_WINHTTP_CANNOT_CONNECT:
		case ERROR_WINHTTP_TIMEOUT:
			rval = Status::ERR_CONNECTION_FAILED;
			break;

		case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
		case ERROR_WINHTTP_CLIENT_CERT_NO_ACCESS_PRIVATE_KEY:
		case ERROR_WINHTTP_CLIENT_CERT_NO_PRIVATE_KEY:
		case ERROR_WINHTTP_REDIRECT_FAILED:
		case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
		case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
		case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
		case ERROR_WINHTTP_SECURE_CERT_REVOKED:
		case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:
		case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
		case ERROR_WINHTTP_SECURE_FAILURE:
		case ERROR_WINHTTP_SECURE_INVALID_CA:
		case ERROR_WINHTTP_SECURE_INVALID_CERT:
			rval = Status::ERR_SECURITY_FAILED;
			break;

		default:
			break;
		}
	}

	return rval;
}