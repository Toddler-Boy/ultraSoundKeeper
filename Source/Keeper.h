#pragma once

#include <windows.h>

#include "Config.h"

//-----------------------------------------------------------------------------

// Keeps the chosen outputs busy: one Stream per output, re-picked when devices come or go, closed while the PC has a
// reason to sleep or the user paused. Lives on the caller's window and message loop

namespace keeper
{
	// settings = the live settings the streams read every buffer, owned by the caller
	bool start ( HWND window, const config::Settings* settings );
	void stop ();

	// After a change of outputs or of open only; everything else the streams pick up by themselves
	void restart ();
	void setPaused ( bool paused );

	// Feed every message of the window here first; true = it was the keeper's
	bool onMessage ( HWND window, UINT message, WPARAM wParam, LPARAM lParam );
}
//-----------------------------------------------------------------------------
