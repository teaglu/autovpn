/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#include "pch.h"
#include "Ip4Network.h"

Ip4Network::Ip4Network(struct in_addr& address, struct in_addr& mask)
{
	this->address = address;
	this->mask = mask;
}

shared_ptr<Ip4Network> Ip4Network::Create(LPCTSTR stringVal)
{
	CString value(stringVal);

	struct in_addr address;
	struct in_addr mask;
	
	shared_ptr<Ip4Network> rval;

	int split = value.Find('/');
	if (split > 0) {
		CString addressPart = value.Mid(0, split);
		CString maskPart = value.Mid(split + 1);

		if (InetPtonW(AF_INET, addressPart, (LPVOID)&address) == 1) {
			if (maskPart.Find('.') > 0) {
				if (InetPtonW(AF_INET, maskPart, (LPVOID)&mask) == 1) {
					address.S_un.S_addr &= mask.S_un.S_addr;
					rval = make_shared<Ip4Network>(address, mask);
				}
			} else {
				int length;
				if (_stscanf_s((LPCTSTR)maskPart, _T("%d"), &length) == 1) {
					if ((length >= 0) && (length <= 32)) {
						mask.S_un.S_addr = htonl(0xFFFFFFFF << (32 - length));
						address.S_un.S_addr &= mask.S_un.S_addr;
						rval = make_shared<Ip4Network>(address, mask);
					}
				}
			}
		}
	}

	return rval;
}

shared_ptr<Ip4Network> Ip4Network::Create(LPCSTR addressPart, LPCSTR maskPart)
{
	struct in_addr address;
	struct in_addr mask;

	shared_ptr<Ip4Network> rval;

	if (InetPtonA(AF_INET, addressPart, (LPVOID)&address) == 1) {
		if (InetPtonA(AF_INET, maskPart, (LPVOID)&mask) == 1) {
			address.S_un.S_addr &= mask.S_un.S_addr;
			rval = make_shared<Ip4Network>(address, mask);
		}
	}

	return rval;
}

bool Ip4Network::equals(Ip4Network* b)
{
	return (address.S_un.S_addr == b->address.S_un.S_addr)
		&& (mask.S_un.S_addr == b->mask.S_un.S_addr);
}

bool Ip4Network::includes(Ip4Network* b)
{
	return
		((mask.S_un.S_addr & ~b->mask.S_un.S_addr) == 0) &&
		((b->address.S_un.S_addr & mask.S_un.S_addr) == address.S_un.S_addr);
}

CString Ip4Network::toString()
{
	CString rval;

	TCHAR addressPart[INET_ADDRSTRLEN + 1];
	TCHAR maskPart[INET_ADDRSTRLEN + 1];

	InetNtopW(AF_INET, &address, addressPart, INET_ADDRSTRLEN);
	InetNtopW(AF_INET, &mask, maskPart, INET_ADDRSTRLEN);

	rval.Append(addressPart);
	rval.Append(_T("/"));
	rval.Append(maskPart);

	return rval;
}
