/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2004 Chris Schoeneman
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

#ifndef COSXKEYSTATE_H
#define COSXKEYSTATE_H

#include "CKeyState.h"
#include "stdmap.h"
#include "stdset.h"
#include "stdvector.h"
#include <Carbon/Carbon.h>

//! OS X key state
/*!
A key state for OS X.
*/
class COSXKeyState : public CKeyState {
public:
	typedef std::vector<KeyID> CKeyIDs;

	COSXKeyState();
	virtual ~COSXKeyState();

	//! Map key event to keys
	/*!
	Converts a key event into a sequence of KeyIDs and the shadow modifier
	state to a modifier mask.  The KeyIDs list, in order, the characters
	generated by the key press/release.  It returns the id of the button
	that was pressed or released, or 0 if the button doesn't map to a known
	KeyID.
	*/
	KeyButton			mapKeyFromEvent(CKeyIDs& ids,
							KeyModifierMask* maskOut, EventRef event) const;

	//! Handle modifier key change
	/*!
	Determines which modifier keys have changed and updates the modifier
	state and sends key events as appropriate.
	*/
	void				handleModifierKeys(void* target,
							KeyModifierMask oldMask, KeyModifierMask newMask);

	// IKeyState overrides
	virtual void		setHalfDuplexMask(KeyModifierMask);
	virtual bool		fakeCtrlAltDel();
	virtual const char*	getKeyName(KeyButton) const;
	virtual void		sendKeyEvent(void* target,
							bool press, bool isAutoRepeat,
							KeyID key, KeyModifierMask mask,
							SInt32 count, KeyButton button);

protected:
	// IKeyState overrides
	virtual void		doUpdateKeys();
	virtual void		doFakeKeyEvent(KeyButton button,
							bool press, bool isAutoRepeat);
	virtual KeyButton	mapKey(Keystrokes& keys, KeyID id,
							KeyModifierMask desiredMask,
							bool isAutoRepeat) const;

private:
	struct CKeyEventInfo {
	public:
		KeyButton		m_button;
		KeyModifierMask	m_requiredMask;
		KeyModifierMask	m_requiredState;
	};
	typedef std::vector<CKeyEventInfo> CKeySequence;
	typedef std::map<KeyID, CKeySequence> CKeyIDMap;
	typedef std::map<UInt32, KeyID> CVirtualKeyMap;
	typedef std::map<UInt16, std::pair<SInt32, KeyModifierMask> > CDeadKeyMap;

	KeyButton			addKeystrokes(Keystrokes& keys,
							KeyButton keyButton,
							KeyModifierMask desiredMask,
							KeyModifierMask requiredMask,
							bool isAutoRepeat) const;
	bool				adjustModifiers(Keystrokes& keys,
							Keystrokes& undo,
							KeyModifierMask desiredMask,
							KeyModifierMask requiredMask) const;
	void				addKeyButton(KeyButtons& keys, KeyID id) const;
	void				handleModifierKey(void* target, KeyID id, bool down);

	// Check if the keyboard layout has changed and call doUpdateKeys
	// if so.
	void				checkKeyboardLayout();

	// Switch to a new keyboard layout.
	void				setKeyboardLayout(SInt16 keyboardLayoutID);

	// Insert KeyID to key sequences for non-printing characters, like
	// delete, home, up arrow, etc. and the virtual key to KeyID mapping.
	void				fillSpecialKeys(CKeyIDMap& keyMap,
							CVirtualKeyMap& virtualKeyMap) const;

	// Convert the KCHR resource to a KeyID to key sequence map.  the
	// map maps each KeyID to the sequence of keys (with modifiers)
	// that would have to be synthesized to generate the KeyID character.
	// Returns false iff no KCHR resource was found.
	bool				fillKCHRKeysMap(CKeyIDMap& keyMap) const;

	// Convert the uchr resource to a KeyID to key sequence map.  the
	// map maps each KeyID to the sequence of keys (with modifiers)
	// that would have to be synthesized to generate the KeyID character.
	// Returns false iff no uchr resource was found or it couldn't be
	// mapped.
	bool				filluchrKeysMap(CKeyIDMap& keyMap) const;

	// Maps an OS X virtual key id to a KeyButton.  This simply remaps
	// the ids so we don't use KeyButton 0.
	static KeyButton	mapVirtualKeyToKeyButton(UInt32 keyCode);

	// Maps a KeyButton to an OS X key code.  This is the inverse of
	// mapVirtualKeyToKeyButton.
	static UInt32		mapKeyButtonToVirtualKey(KeyButton keyButton);

	// Convert a character in the current script to the equivalent KeyID.
	static KeyID		charToKeyID(UInt8);

	// Convert a unicode character to the equivalent KeyID.
	static KeyID		unicharToKeyID(UniChar);

	// Choose the modifier mask with the fewest modifiers for character
	// mapping table i.  The tableSelectors table has numEntries.  If
	// no mapping is found for i, try mapping defaultIndex.
	static KeyModifierMask
						maskForTable(UInt8 i, UInt8* tableSelectors,
							UInt32 numEntries, UInt8 defaultIndex);

	// Save characters built from dead key sequences.
	static void			mapDeadKeySequence(CKeyIDMap& keyMap,
							CKeySequence& sequence,
							UInt16 state, const UInt8* base,
							const UCKeyStateRecordsIndex* sri,
							const UCKeyStateTerminators* st,
							CDeadKeyMap& dkMap);

private:
	// OS X uses a physical key if 0 for the 'A' key.  synergy reserves
	// KeyButton 0 so we offset all OS X physical key ids by this much
	// when used as a KeyButton and by minus this much to map a KeyButton
	// to a physical button.
	enum {
		KeyButtonOffset = 1
	};

	// KCHR resource header
	struct CKCHRResource {
	public:
    	SInt16              m_version;
    	UInt8               m_tableSelectionIndex[256];
    	SInt16              m_numTables;
    	UInt8               m_characterTables[1][128];
	};

	SInt16				m_keyboardLayoutID;
	UInt32				m_keyboardType;
	mutable UInt32		m_deadKeyState;
	Handle				m_KCHRHandle;
	Handle				m_uchrHandle;
	CKCHRResource*		m_KCHRResource;
	UCKeyboardLayout*	m_uchrResource;
	CKeyIDMap			m_keyMap;
	CVirtualKeyMap		m_virtualKeyMap;
	bool				m_uchrFound;
};

#endif
