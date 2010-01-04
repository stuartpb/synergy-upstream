/*
* synergy -- mouse and keyboard sharing utility
* Copyright (C) 2002 Chris Schoeneman
* 
* This package is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* found in the file COPYING that should have accompanied this file.
* 
* This package is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#pragma once

#include "common.h"
#include "CString.h"

class CApp {
public:
	CApp();
	virtual ~CApp();
	CString m_daemonName;
	CString m_daemonInfo;
	void (*m_bye)(int);
};

#define BYE "\nTry `%s --help' for more information."
