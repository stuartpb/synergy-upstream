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

#ifndef CPRIMARYCLIENT_H
#define CPRIMARYCLIENT_H

#include "CBaseClientProxy.h"
#include "ProtocolTypes.h"

class CScreen;

//! Primary screen as pseudo-client
/*!
The primary screen does not have a client associated with it.  This
class provides a pseudo-client to allow the primary screen to be
treated as if it was a client.
*/
class CPrimaryClient : public CBaseClientProxy {
public:
	/*!
	\c name is the name of the server and \p screen is primary screen.
	*/
	CPrimaryClient(const CString& name, CScreen* screen);
	~CPrimaryClient();

	//! @name manipulators
	//@{

	//! Update configuration
	/*!
	Handles reconfiguration of jump zones.
	*/
	void				reconfigure(UInt32 activeSides);

	//! Register a system hotkey
	/*!
	Registers a system-wide hotkey for key \p key with modifiers \p mask.
	Returns an id used to unregister the hotkey.
	*/
	UInt32				registerHotKey(KeyID key, KeyModifierMask mask, UInt8 id);

	//! Unregister a system hotkey
	/*!
	Unregisters a previously registered hot key.
	*/
	void				unregisterHotKey(UInt32 id);

	//! Prepare to synthesize input on primary screen
	/*!
	Prepares the primary screen to receive synthesized input.  We do not
	want to receive this synthesized input as user input so this method
	ensures that we ignore it.  Calls to \c fakeInputBegin() and
	\c fakeInputEnd() may be nested;  only the outermost have an effect.
	*/
	void				fakeInputBegin();

	//! Done synthesizing input on primary screen
	/*!
	Undoes whatever \c fakeInputBegin() did.
	*/
	void				fakeInputEnd();

	//@}
	//! @name accessors
	//@{

	//! Get jump zone size
	/*!
	Return the jump zone size, the size of the regions on the edges of
	the screen that cause the cursor to jump to another screen.
	*/
	SInt32				getJumpZoneSize() const;

	//! Get cursor center position
	/*!
	Return the cursor center position which is where we park the
	cursor to compute cursor motion deltas and should be far from
	the edges of the screen, typically the center.
	*/
	void				getCursorCenter(SInt32& x, SInt32& y) const;
	
	//! Get toggle key state
	/*!
	Returns the primary screen's current toggle modifier key state.
	*/
	KeyModifierMask		getToggleMask(UInt8 id) const;

	//! Get screen lock state
	/*!
	Returns true if the user is locked to the screen.
	*/
	bool				isLockedToScreen(UInt8 id) const;

	//@}

	// FIXME -- these probably belong on IScreen
	virtual void		enable();
	virtual void		disable();

	// IScreen overrides
	virtual void*		getEventTarget() const;
	virtual bool		getClipboard(ClipboardID id, IClipboard*) const;
	virtual void		getShape(SInt32& x, SInt32& y,
							SInt32& width, SInt32& height) const;
	virtual void		getCursorPos(SInt32& x, SInt32& y, UInt8 id) const;

	// IClient overrides
	virtual void		enter(SInt32 xAbs, SInt32 yAbs,	UInt32 seqNum, KeyModifierMask mask,
							bool forScreensaver, UInt8 kId, UInt8 pId);
	virtual bool		leave(UInt8 id);
	virtual void		setClipboard(ClipboardID, const IClipboard*);
	virtual void		grabClipboard(ClipboardID);
	virtual void		setClipboardDirty(ClipboardID, bool);

	virtual void 		mouseDown(ButtonID button, UInt8 id);
	virtual void 		mouseUp(ButtonID button, UInt8 id);
	virtual void 		mouseMove(SInt32 xAbs, SInt32 yAbs, UInt8 id);
	virtual void 		mouseRelativeMove(SInt32 xAbs, SInt32 yAbs, UInt8 id);
	virtual void 		mouseWheel(SInt32 xDelta, SInt32 yDelta, UInt8 id);
	virtual void		keyDown(KeyID, KeyModifierMask, KeyButton, UInt8 id);
	virtual void		keyRepeat(KeyID, KeyModifierMask, SInt32 count, KeyButton, UInt8 id);
	virtual void		keyUp(KeyID, KeyModifierMask, KeyButton, UInt8 id);
	virtual void		screensaver(bool activate);
	virtual void		resetOptions();
	virtual void		setOptions(const COptionsList& options);

private:
	CScreen*			m_screen;
	bool				m_clipboardDirty[kClipboardEnd];
	SInt32				m_fakeInputCount;
};

#endif
