/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#pragma once

#define REG_COMPANY _T("Teaglu")
#define REG_PRODUCT _T("AutoVPN")

class Settings
{
public:
	Settings();
	virtual ~Settings();

	HKEY getPolicyRoot() { return policyRoot; }
	HKEY getPreferenceRoot() { return preferenceRoot; };

	static bool readString(HKEY, LPCTSTR, CString&);
	static bool readInt(HKEY, LPCTSTR, int&);

	bool readString(LPCTSTR, CString&);
	bool readInt(LPCTSTR, int&);

	Settings getSubKey(LPCTSTR name);

private:
	Settings(HKEY policyRoot, HKEY preferenceRoot);

	void loadPolicyRoot();
	void loadPreferenceRoot();

	HKEY policyRoot;
	HKEY preferenceRoot;
};

