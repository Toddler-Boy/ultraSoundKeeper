#include <algorithm>
#include <array>
#include <span>

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <windowsx.h>

#include "Autostart.h"
#include "Config.h"
#include "GUI_TrayIcon.h"
#include "Keeper.h"
#include "Text.h"

//-----------------------------------------------------------------------------

namespace
{
	constexpr auto	trayMessage = UINT ( WM_APP + 1 );
	constexpr auto	trayUid = UINT ( 1 );

	// Shared with Sound Keeper on purpose: starting this stops a running one, and the other way round
	constexpr auto	stopEventName = L"SoundKeeperStopEvent";
	constexpr auto	mutexName = L"SoundKeeperMutex";

	// Menu command ids, the radio groups take a range each
	constexpr auto	commandMode = UINT ( 1 );
	constexpr auto	commandFrequency = UINT ( 20 );
	constexpr auto	commandVolume = UINT ( 40 );
	constexpr auto	commandOutputs = UINT ( 60 );
	constexpr auto	commandSleepDisplay = UINT ( 80 );
	constexpr auto	commandSleepLock = UINT ( 81 );
	constexpr auto	commandAutostart = UINT ( 82 );
	constexpr auto	commandPause = UINT ( 83 );
	constexpr auto	commandExit = UINT ( 84 );

	struct Option
	{
		int				value;
		const wchar_t*	label;
	};

	constexpr auto	modeNames = std::array { L"Open only", L"Silence", L"Fluctuate", L"Sine", L"White noise", L"Pink noise" };
	constexpr auto	outputNames = std::array { L"Default", L"All", L"Digital", L"Analog" };
	constexpr auto	outputTips = std::array { L"the default output", L"all outputs", L"digital outputs", L"analog outputs" };
	constexpr auto	frequencies = std::array<Option, 6> { { { 1, L"1 Hz" }, { 5, L"5 Hz" }, { 10, L"10 Hz" }, { 20, L"20 Hz" }, { 50, L"50 Hz" }, { 100, L"100 Hz" } } };
	constexpr auto	volumes = std::array<Option, 5> { { { 1, L"0.1 %" }, { 5, L"0.5 %" }, { 10, L"1 %" }, { 50, L"5 %" }, { 100, L"10 %" } } };

	// The live settings: the streams read them every buffer, the menu writes them
	auto	settings = config::Settings {};
	auto	paused = false;
	auto	window = HWND ( nullptr );
	auto	taskbarCreated = UINT ( 0 );

	// Dark context menus: undocumented uxtheme exports by ordinal, 0 default, 1 allow dark, 2 force dark, 3 force light
	using SetPreferredAppMode = int ( WINAPI* ) ( int );
	using FlushMenuThemes = void ( WINAPI* ) ();
	auto	flushMenuThemes = FlushMenuThemes ( nullptr );

	void allowDarkMenus ()
	{
		const auto	uxtheme = LoadLibraryExW ( L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
		if ( uxtheme == nullptr )
			return;

		const auto	setPreferredAppMode = reinterpret_cast<SetPreferredAppMode> ( GetProcAddress ( uxtheme, MAKEINTRESOURCEA ( 135 ) ) );
		flushMenuThemes = reinterpret_cast<FlushMenuThemes> ( GetProcAddress ( uxtheme, MAKEINTRESOURCEA ( 136 ) ) );
		if ( setPreferredAppMode != nullptr )
			setPreferredAppMode ( 1 );
		if ( flushMenuThemes != nullptr )
			flushMenuThemes ();
	}

	bool usesFrequency ( config::Mode mode )
	{
		return mode == config::Mode::fluctuate || mode == config::Mode::sine;
	}

	bool usesVolume ( config::Mode mode )
	{
		return mode == config::Mode::sine || mode == config::Mode::whiteNoise || mode == config::Mode::pinkNoise;
	}

	bool lightTaskbar ()
	{
		auto	light = DWORD ( 0 );
		auto	size = DWORD ( sizeof ( light ) );
		RegGetValueW ( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme",
					   RRF_RT_REG_DWORD, nullptr, &light, &size );
		return light != 0;
	}

	NOTIFYICONDATAW iconData ()
	{
		auto	data = NOTIFYICONDATAW {};
		data.cbSize = sizeof ( data );
		data.hWnd = window;
		data.uID = trayUid;
		return data;
	}

	// Modify falls back to add, which also covers a failed add at logon and a restarted Explorer
	void showIcon ()
	{
		auto	data = iconData ();
		data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
		data.uCallbackMessage = trayMessage;
		data.hIcon = trayIcon::create ( paused, GetSystemMetricsForDpi ( SM_CXSMICON, GetDpiForWindow ( window ) ), lightTaskbar () );

		if ( paused )
			wsprintfW ( data.szTip, L"ultraSoundKeeper\nPaused" );
		else
			wsprintfW ( data.szTip, L"ultraSoundKeeper\n%s on %s", modeNames[ int ( settings.mode ) ], outputTips[ int ( settings.outputs ) ] );

		if ( Shell_NotifyIconW ( NIM_MODIFY, &data ) == FALSE && Shell_NotifyIconW ( NIM_ADD, &data ) != FALSE )
		{
			data.uVersion = NOTIFYICON_VERSION_4;
			Shell_NotifyIconW ( NIM_SETVERSION, &data );
		}

		DestroyIcon ( data.hIcon );
	}

	void removeIcon ()
	{
		auto	data = iconData ();
		Shell_NotifyIconW ( NIM_DELETE, &data );
	}

	HMENU radioMenu ( std::span<const wchar_t* const> labels, UINT firstCommand, int checked )
	{
		const auto	menu = CreatePopupMenu ();
		for ( auto i = 0u; i < labels.size (); ++i )
			AppendMenuW ( menu, MF_STRING, firstCommand + i, labels[ i ] );

		if ( checked >= 0 )
			CheckMenuRadioItem ( menu, firstCommand, firstCommand + UINT ( labels.size () ) - 1, firstCommand + UINT ( checked ), MF_BYCOMMAND );

		return menu;
	}

	HMENU radioMenu ( std::span<const Option> options, UINT firstCommand, int value )
	{
		auto	labels = std::array<const wchar_t*, 8> {};
		for ( auto i = 0u; i < options.size (); ++i )
			labels[ i ] = options[ i ].label;

		const auto	it = std::ranges::find ( options, value, &Option::value );
		return radioMenu ( std::span ( labels.data (), options.size () ), firstCommand, it != options.end () ? int ( it - options.begin () ) : -1 );
	}

	// The streams pick mode, frequency and volume up by themselves; the device set and open only need a restart
	void onCommand ( UINT command )
	{
		const auto	previous = settings;

		if ( command >= commandMode && command < commandMode + modeNames.size () )
			settings.mode = config::Mode ( command - commandMode );
		else if ( command >= commandFrequency && command < commandFrequency + frequencies.size () )
			settings.frequencyHz = frequencies[ command - commandFrequency ].value;
		else if ( command >= commandVolume && command < commandVolume + volumes.size () )
			settings.volumeTenths = volumes[ command - commandVolume ].value;
		else if ( command >= commandOutputs && command < commandOutputs + outputNames.size () )
			settings.outputs = config::Outputs ( command - commandOutputs );
		else if ( command == commandSleepDisplay )
			settings.sleepWithDisplay = ! settings.sleepWithDisplay;
		else if ( command == commandSleepLock )
			settings.sleepWithLock = ! settings.sleepWithLock;
		else if ( command == commandAutostart )
			autostart::setEnabled ( ! autostart::isEnabled () );
		else if ( command == commandPause )
		{
			paused = ! paused;
			keeper::setPaused ( paused );
		}
		else if ( command == commandExit )
		{
			DestroyWindow ( window );
			return;
		}

		config::save ( settings );

		const auto	openOnly = settings.mode == config::Mode::openOnly;
		const auto	wasOpenOnly = previous.mode == config::Mode::openOnly;
		if ( settings.outputs != previous.outputs || openOnly != wasOpenOnly )
			keeper::restart ();

		showIcon ();
	}

	// anchor = where the shell wants the menu, the cursor or the icon when opened by keyboard
	void showMenu ( POINT anchor )
	{
		const auto	mode = settings.mode;
		const auto	menu = CreatePopupMenu ();
		AppendMenuW ( menu, MF_STRING | MF_GRAYED, 0, L"ultraSoundKeeper " APP_VERSION );
		AppendMenuW ( menu, MF_SEPARATOR, 0, nullptr );
		AppendMenuW ( menu, MF_POPUP, UINT_PTR ( radioMenu ( modeNames, commandMode, int ( mode ) ) ), L"Mode" );
		AppendMenuW ( menu, MF_POPUP | ( usesFrequency ( mode ) ? 0 : MF_GRAYED ), UINT_PTR ( radioMenu ( frequencies, commandFrequency, settings.frequencyHz ) ), L"Frequency" );
		AppendMenuW ( menu, MF_POPUP | ( usesVolume ( mode ) ? 0 : MF_GRAYED ), UINT_PTR ( radioMenu ( volumes, commandVolume, settings.volumeTenths ) ), L"Volume" );
		AppendMenuW ( menu, MF_POPUP, UINT_PTR ( radioMenu ( outputNames, commandOutputs, int ( settings.outputs ) ) ), L"Outputs" );

		const auto	sleep = CreatePopupMenu ();
		AppendMenuW ( sleep, MF_STRING | ( settings.sleepWithDisplay ? MF_CHECKED : 0 ), commandSleepDisplay, L"When the display is off" );
		AppendMenuW ( sleep, MF_STRING | ( settings.sleepWithLock ? MF_CHECKED : 0 ), commandSleepLock, L"When the session is locked" );
		AppendMenuW ( menu, MF_POPUP, UINT_PTR ( sleep ), L"Sleep" );

		AppendMenuW ( menu, MF_SEPARATOR, 0, nullptr );
		AppendMenuW ( menu, MF_STRING | ( autostart::isEnabled () ? MF_CHECKED : 0 ), commandAutostart, L"Start with Windows" );
		AppendMenuW ( menu, MF_STRING, commandPause, paused ? L"Resume" : L"Pause" );
		AppendMenuW ( menu, MF_SEPARATOR, 0, nullptr );
		AppendMenuW ( menu, MF_STRING, commandExit, L"Exit" );

		SetForegroundWindow ( window );
		const auto	choice = TrackPopupMenu ( menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, anchor.x, anchor.y, 0, window, nullptr );
		PostMessageW ( window, WM_NULL, 0, 0 );
		DestroyMenu ( menu );

		if ( choice != 0 )
			onCommand ( UINT ( choice ) );
	}

	LRESULT CALLBACK windowProc ( HWND target, UINT message, WPARAM wParam, LPARAM lParam )
	{
		if ( keeper::onMessage ( target, message, wParam, lParam ) )
			return 0;

		if ( message == taskbarCreated )
		{
			showIcon ();
			return 0;
		}

		switch ( message )
		{
			case WM_SETTINGCHANGE:
				if ( lParam != 0 && text::equals ( reinterpret_cast<const wchar_t*> ( lParam ), L"ImmersiveColorSet" ) )
				{
					if ( flushMenuThemes != nullptr )
						flushMenuThemes ();
					showIcon ();
				}
				return 0;

			case WM_DISPLAYCHANGE:
			case WM_DPICHANGED:
				showIcon ();
				return 0;

			case trayMessage:
				switch ( LOWORD ( lParam ) )
				{
					case WM_CONTEXTMENU:
					case NIN_SELECT:
					case NIN_KEYSELECT:
						showMenu ( { GET_X_LPARAM ( wParam ), GET_Y_LPARAM ( wParam ) } );
				}
				return 0;

			case WM_DESTROY:
				removeIcon ();
				PostQuitMessage ( 0 );
				return 0;
		}

		return DefWindowProcW ( target, message, wParam, lParam );
	}

	// A hidden top-level window, message-only windows miss the TaskbarCreated broadcast
	HWND createWindow ()
	{
		const auto	instance = GetModuleHandleW ( nullptr );

		auto	windowClass = WNDCLASSW {};
		windowClass.lpfnWndProc = windowProc;
		windowClass.hInstance = instance;
		windowClass.lpszClassName = L"ultraSoundKeeper";
		RegisterClassW ( &windowClass );

		return CreateWindowExW ( 0, windowClass.lpszClassName, L"ultraSoundKeeper", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr );
	}

	// Another instance (or Sound Keeper, or "kill") asks this one to leave through the named event
	void CALLBACK onStopEvent ( void*, BOOLEAN )
	{
		PostMessageW ( window, WM_CLOSE, 0, 0 );
	}

	// true = this is now the only instance
	bool claimInstance ( HANDLE stopEvent, HANDLE mutex )
	{
		if ( GetLastError () != ERROR_ALREADY_EXISTS )
			return true;

		SetEvent ( stopEvent );
		const auto	taken = WaitForSingleObject ( mutex, 1000 ) != WAIT_TIMEOUT;
		ResetEvent ( stopEvent );
		return taken;
	}

	int run ()
	{
		const auto	stopEvent = CreateEventW ( nullptr, TRUE, FALSE, stopEventName );
		const auto	mutex = CreateMutexW ( nullptr, TRUE, mutexName );
		if ( stopEvent == nullptr || mutex == nullptr )
			return 1;

		if ( text::equals ( text::arguments (), L"kill", true ) )
		{
			SetEvent ( stopEvent );
			return 0;
		}

		if ( ! claimInstance ( stopEvent, mutex ) )
			return 1;

		if ( FAILED ( CoInitializeEx ( nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE ) ) )
			return 1;

		autostart::initialise ();
		settings = config::load ();
		allowDarkMenus ();
		SetProcessDpiAwarenessContext ( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
		taskbarCreated = RegisterWindowMessageW ( L"TaskbarCreated" );

		window = createWindow ();
		if ( window == nullptr )
			return 1;

		auto	stopWait = HANDLE ( nullptr );
		RegisterWaitForSingleObject ( &stopWait, stopEvent, onStopEvent, nullptr, INFINITE, WT_EXECUTEONLYONCE );

		if ( ! keeper::start ( window, &settings ) )
		{
			MessageBoxW ( nullptr, L"Cannot initialize WASAPI.", L"ultraSoundKeeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL );
			return 1;
		}

		showIcon ();

		auto	message = MSG {};
		while ( GetMessageW ( &message, nullptr, 0, 0 ) > 0 )
			DispatchMessageW ( &message );

		keeper::stop ();
		UnregisterWait ( stopWait );
		CoUninitialize ();
		ReleaseMutex ( mutex );
		return 0;
	}
}
//-----------------------------------------------------------------------------

// The linker's entry point (/ENTRY), there is no C runtime to call a WinMain
extern "C" void entry ()
{
	ExitProcess ( UINT ( run () ) );
}
//-----------------------------------------------------------------------------
