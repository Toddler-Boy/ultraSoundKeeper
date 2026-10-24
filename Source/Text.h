#pragma once

#include <windows.h>

//-----------------------------------------------------------------------------

// Wide string helpers on top of kernel32, Release links no C runtime of its own

namespace text
{
	inline bool equals ( const wchar_t* a, const wchar_t* b, bool ignoreCase = false )
	{
		return CompareStringOrdinal ( a, -1, b, -1, ignoreCase ) == CSTR_EQUAL;
	}

	// The command line after the program name, quoted or not
	inline const wchar_t* arguments ()
	{
		auto*	text = GetCommandLineW ();
		if ( *text == L'"' )
		{
			for ( ++text; *text != 0 && *text != L'"'; ++text );
			if ( *text == L'"' )
				++text;
		}
		else
			for ( ; *text != 0 && *text != L' '; ++text );

		for ( ; *text == L' '; ++text );
		return text;
	}
}
//-----------------------------------------------------------------------------
