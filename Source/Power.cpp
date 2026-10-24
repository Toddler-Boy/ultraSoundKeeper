#define INITGUID

#include <windows.h>
#include <powrprof.h>
#include <wtsapi32.h>

#include "Power.h"

//-----------------------------------------------------------------------------

namespace
{
	auto	window = HWND ( nullptr );
	auto	message = UINT ( 0 );
	auto	suspendResume = HPOWERNOTIFY ( nullptr );
	auto	displayState = HPOWERNOTIFY ( nullptr );

	void post ( power::Event event )
	{
		PostMessageW ( window, message, WPARAM ( event ), 0 );
	}

	// Only a resume by the user counts, an unattended one goes back to sleep anyway
	ULONG CALLBACK onSuspendResume ( void*, ULONG type, void* )
	{
		if ( type == PBT_APMSUSPEND )
			post ( power::Event::suspend );
		else if ( type == PBT_APMRESUMESUSPEND )
			post ( power::Event::resume );

		return ERROR_SUCCESS;
	}

	// Dim counts as on
	ULONG CALLBACK onPowerSetting ( void*, ULONG type, void* setting )
	{
		if ( type != PBT_POWERSETTINGCHANGE )
			return ERROR_SUCCESS;

		const auto*	change = static_cast<const POWERBROADCAST_SETTING*> ( setting );
		if ( change->PowerSetting == GUID_CONSOLE_DISPLAY_STATE && change->DataLength >= sizeof ( DWORD ) )
			post ( *reinterpret_cast<const DWORD*> ( change->Data ) == PowerMonitorOff ? power::Event::displayOff : power::Event::displayOn );

		return ERROR_SUCCESS;
	}
}
//-----------------------------------------------------------------------------

bool power::watch ( HWND target, UINT notification )
{
	window = target;
	message = notification;

	auto	params = DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS { onSuspendResume, nullptr };
	PowerRegisterSuspendResumeNotification ( DEVICE_NOTIFY_CALLBACK, reinterpret_cast<HANDLE> ( &params ), &suspendResume );

	params.Callback = onPowerSetting;
	PowerSettingRegisterNotification ( &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_CALLBACK, reinterpret_cast<HANDLE> ( &params ), &displayState );

	return WTSRegisterSessionNotification ( window, NOTIFY_FOR_THIS_SESSION ) != FALSE;
}

void power::unwatch ( HWND target )
{
	WTSUnRegisterSessionNotification ( target );

	if ( displayState != nullptr )
		PowerSettingUnregisterNotification ( displayState );
	if ( suspendResume != nullptr )
		PowerUnregisterSuspendResumeNotification ( suspendResume );

	displayState = nullptr;
	suspendResume = nullptr;
}

bool power::sessionLocked ()
{
	auto*	info = static_cast<WTSINFOEXW*> ( nullptr );
	auto	bytes = DWORD ( 0 );
	if ( WTSQuerySessionInformationW ( WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, reinterpret_cast<LPWSTR*> ( &info ), &bytes ) == FALSE )
		return false;

	const auto	locked = bytes >= sizeof ( WTSINFOEXW ) && info->Level == 1 && info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK;
	WTSFreeMemory ( info );
	return locked;
}
//-----------------------------------------------------------------------------
