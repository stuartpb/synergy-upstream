/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2002 Chris Schoeneman, Nick Bolton, Sorin Sbarnea
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#pragma once

#include "IArchAppUtil.h"
#include "XSynergy.h"

class CArchAppUtil : public IArchAppUtil {
public:
	CArchAppUtil();
	virtual ~CArchAppUtil();
	
	virtual bool parseArg(const int& argc, const char* const* argv, int& i);
	virtual void adoptApp(IApp* app);
	IApp& app() const;
	virtual void exitApp(int code) { throw XExitApp(code); }

	static CArchAppUtil& instance();
	static void exitAppStatic(int code) { instance().exitApp(code); }
	virtual void beforeAppExit() {}
	
private:
	IApp* m_app;
	static CArchAppUtil* s_instance;
};