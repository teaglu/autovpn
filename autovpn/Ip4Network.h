/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#pragma once
class Ip4Network {
public:
	Ip4Network(struct in_addr &, struct in_addr &);

	static shared_ptr<Ip4Network> Create(LPCTSTR);
	static shared_ptr<Ip4Network> Create(LPCSTR, LPCSTR);

	bool equals(Ip4Network *);
	bool includes(Ip4Network *);

	CString toString();

private:
	struct in_addr address;
	struct in_addr mask;
};

