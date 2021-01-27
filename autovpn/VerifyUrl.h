/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#pragma once
class VerifyUrl {
public:
	enum class Status {
		UNKNOWN = 0,
		SUCCESS = 1,
		ERR_UNKNOWN = 2,
		ERR_DNS_FAILED = 3,
		ERR_CONNECTION_FAILED = 4,
		ERR_SECURITY_FAILED = 5,
		ERR_WRONG_CONTENT = 6
	};

	static Status verifyUrl(LPCTSTR url, LPCTSTR expected);
};

