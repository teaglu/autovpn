/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#include "pch.h"
#include "Log.h"
#include "Settings.h"

Settings::Settings()
{
	loadPolicyRoot();
	loadPreferenceRoot();
}

Settings::Settings(HKEY policyRoot, HKEY preferenceRoot)
{
	this->policyRoot = policyRoot;
	this->preferenceRoot = preferenceRoot;
}

Settings Settings::getSubKey(LPCTSTR name)
{
	HKEY subPolicyRoot = NULL;
	HKEY subPreferenceRoot = NULL;

	LSTATUS regStatus = RegOpenKeyEx(policyRoot, name, 0, GENERIC_READ, &subPolicyRoot);
	if (regStatus != ERROR_SUCCESS) {
		subPolicyRoot = NULL;
	}
	regStatus = RegOpenKeyEx(preferenceRoot, name, 0, GENERIC_READ, &subPreferenceRoot);
	if (regStatus != ERROR_SUCCESS) {
		subPolicyRoot = NULL;
	}

	// Do in one line instead of creating an object or we get in a pickle
	// with the implicit copy constructor using the same handles and the
	// destructor on the temp object closing the handles.
	return Settings(subPolicyRoot, subPreferenceRoot);
}

Settings::~Settings()
{
	if (policyRoot != NULL) {
		RegCloseKey(policyRoot);
	}
	if (preferenceRoot != NULL) {
		RegCloseKey(preferenceRoot);
	}
}

void Settings::loadPreferenceRoot()
{
	preferenceRoot = NULL;

	HKEY software;
	DWORD rval = 0;
	rval = RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("Software"), 0, KEY_READ | KEY_CREATE_SUB_KEY, &software);
	if (rval != ERROR_SUCCESS) {
		Log::log(LOG_ERROR, _T("Error %08X opening Software key"), rval);
	} else {
		HKEY company;
		DWORD disposition;
		rval = RegCreateKeyEx(software, REG_COMPANY,
			0, NULL, 0, KEY_ALL_ACCESS, NULL, &company, &disposition);
		if (rval != ERROR_SUCCESS) {
			Log::log(LOG_ERROR, _T("Error %08X opening company registry key"), rval);
		} else {
			rval = RegCreateKeyEx(company, REG_PRODUCT,
				0, NULL, 0, KEY_ALL_ACCESS, NULL, &preferenceRoot, &disposition);
			if (rval != ERROR_SUCCESS) {
				Log::log(LOG_ERROR, _T("Error %08X opening software registry key"), rval);
				preferenceRoot = NULL;
			}
			RegCloseKey(company);
		}
		RegCloseKey(software);
	}
}

void Settings::loadPolicyRoot()
{
	policyRoot = NULL;

	HKEY software;
	DWORD rval = 0;
	rval = RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("Software"), 0, KEY_READ, &software);
	if (rval == ERROR_SUCCESS) {
		HKEY policies;
		rval = RegOpenKeyEx(software, _T("Policies"), 0, KEY_READ, &policies);
		if (rval == ERROR_SUCCESS) {
			HKEY company;
			rval = RegOpenKeyEx(policies, REG_COMPANY, 0, KEY_READ, &company);
			if (rval == ERROR_SUCCESS) {
				rval = RegOpenKeyEx(company, REG_PRODUCT, 0, KEY_READ, &policyRoot);
				if (rval != ERROR_SUCCESS) {
					policyRoot = NULL;
				}
				RegCloseKey(company);
			}
			RegCloseKey(policies);
		}
		RegCloseKey(software);
	}
}

#define MAX_VALUE 255

bool Settings::readString(HKEY root, LPCTSTR name, CString& value)
{
	bool rval = false;

	DWORD regType = REG_SZ;

	TCHAR buffer[MAX_VALUE + 1];
	DWORD bufferLen = MAX_VALUE * sizeof(TCHAR);

	if (RegQueryValueEx(root, name, NULL, &regType, (LPBYTE)buffer, &bufferLen) == ERROR_SUCCESS) {
		if (regType == REG_SZ) {
			buffer[bufferLen] = '\0';
			value = buffer;
			rval = true;
		}
	}

	return rval;
}

bool Settings::readString(LPCTSTR name, CString& value)
{
	bool rval = false;

	if (policyRoot != NULL) {
		rval = readString(policyRoot, name, value);
	}
	if (!rval && (preferenceRoot != NULL)) {
		rval = readString(preferenceRoot, name, value);
	}
	return rval;
}

bool Settings::readInt(HKEY root, LPCTSTR name, int& value)
{
	bool rval = false;

	DWORD regType = REG_DWORD;

	DWORD buffer;
	DWORD bufferLen = sizeof(DWORD);

	if (RegQueryValueEx(root, name, NULL, &regType, (LPBYTE)&buffer, &bufferLen) == ERROR_SUCCESS) {
		if ((regType == REG_DWORD) && (bufferLen == sizeof(DWORD))) {
			value = (int)buffer;
			rval = true;
		}
	}

	return rval;
}

bool Settings::readInt(LPCTSTR name, int& value)
{
	bool rval = false;

	if (policyRoot != NULL) {
		rval = readInt(policyRoot, name, value);
	}
	if (!rval && (preferenceRoot != NULL)) {
		rval = readInt(preferenceRoot, name, value);
	}
	return rval;
}