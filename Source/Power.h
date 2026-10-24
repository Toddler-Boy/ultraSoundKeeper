#pragma once

#include <windows.h>

//-----------------------------------------------------------------------------

namespace power
{
	enum class Event
	{
		suspend,
		resume,
		displayOff,
		displayOn
	};

	// Posts message with the Event as wParam on every change. Session lock and unlock arrive as WM_WTSSESSION_CHANGE
	bool watch ( HWND window, UINT message );
	void unwatch ( HWND window );

	[[ nodiscard ]] bool sessionLocked ();
}
//-----------------------------------------------------------------------------
