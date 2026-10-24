#include <algorithm>

#include <windows.h>

#include "Config.h"

//-----------------------------------------------------------------------------

namespace
{
	constexpr auto	appKey = L"Software\\ultraSoundKeeper";

	DWORD read ( const wchar_t* name, DWORD fallback )
	{
		auto	value = DWORD ( 0 );
		auto	size = DWORD ( sizeof ( value ) );
		return RegGetValueW ( HKEY_CURRENT_USER, appKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size ) == ERROR_SUCCESS ? value : fallback;
	}

	void write ( const wchar_t* name, DWORD value )
	{
		RegSetKeyValueW ( HKEY_CURRENT_USER, appKey, name, REG_DWORD, &value, sizeof ( value ) );
	}
}
//-----------------------------------------------------------------------------

config::Settings config::load ()
{
	return {
		Mode ( std::min ( read ( L"Mode", DWORD ( Mode::silence ) ), DWORD ( Mode::pinkNoise ) ) ),
		Outputs ( std::min ( read ( L"Outputs", DWORD ( Outputs::primary ) ), DWORD ( Outputs::analog ) ) ),
		int ( read ( L"Frequency", 50 ) ),
		int ( read ( L"Volume", 10 ) ),
		read ( L"SleepWithDisplay", 0 ) != 0,
		read ( L"SleepWithLock", 0 ) != 0 };
}

void config::save ( const Settings& settings )
{
	write ( L"Mode", DWORD ( settings.mode ) );
	write ( L"Outputs", DWORD ( settings.outputs ) );
	write ( L"Frequency", DWORD ( settings.frequencyHz ) );
	write ( L"Volume", DWORD ( settings.volumeTenths ) );
	write ( L"SleepWithDisplay", settings.sleepWithDisplay ? 1 : 0 );
	write ( L"SleepWithLock", settings.sleepWithLock ? 1 : 0 );
}
//-----------------------------------------------------------------------------
