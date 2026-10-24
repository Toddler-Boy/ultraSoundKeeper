#pragma once

#include <windows.h>

//-----------------------------------------------------------------------------

namespace trayIcon
{
	// A waveform of rounded bars, flat while paused; caller destroys it
	[[ nodiscard ]] HICON create ( bool paused, int size, bool lightTaskbar );
}
//-----------------------------------------------------------------------------
