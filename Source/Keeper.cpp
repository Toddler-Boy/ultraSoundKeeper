#define INITGUID

#include <array>
#include <span>

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <objbase.h>
#include <propidl.h>
#include <wtsapi32.h>

#include "Heap.h"
#include "Keeper.h"
#include "Power.h"
#include "Stream.h"

//-----------------------------------------------------------------------------

namespace
{
	constexpr auto	maxStreams = 16;
	constexpr auto	deviceMessage = UINT ( WM_APP + 2 );
	constexpr auto	powerMessage = UINT ( WM_APP + 3 );
	constexpr auto	restartTimer = UINT_PTR ( 1 );
	constexpr auto	restartSettleMilliseconds = UINT ( 200 );

	// Any device coming, going, changing state or becoming the default gets the outputs re-picked; a burst of events
	// collapses into one restart through the timer
	struct DeviceListener final : IMMNotificationClient
	{
		HWND	window;

		HRESULT STDMETHODCALLTYPE QueryInterface ( REFIID id, void** object ) override
		{
			if ( object == nullptr )
				return E_POINTER;

			if ( id != IID_IUnknown && id != __uuidof ( IMMNotificationClient ) )
			{
				*object = nullptr;
				return E_NOINTERFACE;
			}

			*object = static_cast<IMMNotificationClient*> ( this );
			return S_OK;
		}

		ULONG STDMETHODCALLTYPE AddRef () override { return 1; }
		ULONG STDMETHODCALLTYPE Release () override { return 1; }

		HRESULT STDMETHODCALLTYPE OnDeviceStateChanged ( LPCWSTR, DWORD ) override { return changed (); }
		HRESULT STDMETHODCALLTYPE OnDeviceAdded ( LPCWSTR ) override { return changed (); }
		HRESULT STDMETHODCALLTYPE OnDeviceRemoved ( LPCWSTR ) override { return changed (); }
		HRESULT STDMETHODCALLTYPE OnPropertyValueChanged ( LPCWSTR, const PROPERTYKEY ) override { return S_OK; }

		HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged ( EDataFlow flow, ERole role, LPCWSTR ) override
		{
			return flow == eRender && role == eConsole ? changed () : S_OK;
		}

		HRESULT changed ()
		{
			PostMessageW ( window, deviceMessage, 0, 0 );
			return S_OK;
		}
	};

	auto	window = HWND ( nullptr );
	auto	settings = static_cast<const config::Settings*> ( nullptr );
	auto	enumerator = static_cast<IMMDeviceEnumerator*> ( nullptr );
	auto	listener = static_cast<DeviceListener*> ( nullptr );
	auto	streams = std::array<Stream*, maxStreams> {};
	auto	streamCount = 0;
	auto	open = false;
	auto	suspended = false;
	auto	displayOff = false;
	auto	locked = false;
	auto	paused = false;

	// Remote desktop audio is never kept; digital = HDMI and SPDIF
	bool wanted ( IMMDevice* device )
	{
		auto*	store = static_cast<IPropertyStore*> ( nullptr );
		if ( FAILED ( device->OpenPropertyStore ( STGM_READ, &store ) ) )
			return false;

		auto		value = PROPVARIANT {};
		const auto	known = SUCCEEDED ( store->GetValue ( PKEY_AudioEndpoint_FormFactor, &value ) ) && value.vt == VT_UI4;
		const auto	formFactor = known ? value.uintVal : UINT ( UnknownFormFactor );
		PropVariantClear ( &value );
		store->Release ();

		if ( ! known || formFactor == RemoteNetworkDevice )
			return false;

		const auto	digital = formFactor == SPDIF || formFactor == DigitalAudioDisplayDevice;
		switch ( settings->outputs )
		{
			case config::Outputs::digital:	return digital;
			case config::Outputs::analog:	return ! digital;
			default:						return true;
		}
	}

	void addStream ( IMMDevice* device )
	{
		if ( streamCount == maxStreams || ! wanted ( device ) )
			return;

		auto*	stream = heap::create<Stream> ();
		if ( stream == nullptr )
			return;

		if ( stream->start ( device, settings ) )
			streams[ streamCount++ ] = stream;
		else
			heap::destroy ( stream );
	}

	void openStreams ()
	{
		if ( open )
			return;
		open = true;

		if ( settings->outputs == config::Outputs::primary )
		{
			auto*	device = static_cast<IMMDevice*> ( nullptr );
			if ( SUCCEEDED ( enumerator->GetDefaultAudioEndpoint ( eRender, eConsole, &device ) ) )
			{
				addStream ( device );
				device->Release ();
			}
			return;
		}

		auto*	collection = static_cast<IMMDeviceCollection*> ( nullptr );
		if ( FAILED ( enumerator->EnumAudioEndpoints ( eRender, DEVICE_STATE_ACTIVE, &collection ) ) )
			return;

		auto	count = UINT ( 0 );
		collection->GetCount ( &count );
		for ( auto i = 0u; i < count; ++i )
		{
			auto*	device = static_cast<IMMDevice*> ( nullptr );
			if ( SUCCEEDED ( collection->Item ( i, &device ) ) )
			{
				addStream ( device );
				device->Release ();
			}
		}

		collection->Release ();
	}

	void closeStreams ()
	{
		for ( auto* stream : std::span ( streams.data (), size_t ( streamCount ) ) )
		{
			stream->stop ();
			heap::destroy ( stream );
		}

		streamCount = 0;
		open = false;
	}

	bool asleep ()
	{
		return suspended || displayOff || locked || paused;
	}

	void update ()
	{
		if ( asleep () )
			closeStreams ();
		else
			openStreams ();
	}

	void scheduleRestart ()
	{
		SetTimer ( window, restartTimer, restartSettleMilliseconds, nullptr );
	}
}
//-----------------------------------------------------------------------------

bool keeper::start ( HWND owner, const config::Settings* live )
{
	window = owner;
	settings = live;

	if ( FAILED ( CoCreateInstance ( __uuidof ( MMDeviceEnumerator ), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS ( &enumerator ) ) ) )
		return false;

	listener = heap::create<DeviceListener> ();
	if ( listener == nullptr )
		return false;

	listener->window = window;
	enumerator->RegisterEndpointNotificationCallback ( listener );
	power::watch ( window, powerMessage );

	locked = settings->sleepWithLock && power::sessionLocked ();
	update ();
	return true;
}

void keeper::stop ()
{
	closeStreams ();
	power::unwatch ( window );

	if ( enumerator != nullptr )
	{
		enumerator->UnregisterEndpointNotificationCallback ( listener );
		enumerator->Release ();
	}
	enumerator = nullptr;

	heap::destroy ( listener );
	listener = nullptr;
}

void keeper::restart ()
{
	scheduleRestart ();
}

void keeper::setPaused ( bool pause )
{
	paused = pause;
	update ();
}

bool keeper::onMessage ( HWND, UINT message, WPARAM wParam, LPARAM )
{
	switch ( message )
	{
		case deviceMessage:
			scheduleRestart ();
			return true;

		case WM_TIMER:
			if ( wParam != restartTimer )
				return false;
			KillTimer ( window, restartTimer );
			closeStreams ();
			update ();
			return true;

		case powerMessage:
			switch ( power::Event ( wParam ) )
			{
				case power::Event::suspend:		suspended = true; break;
				case power::Event::resume:		suspended = false; break;
				case power::Event::displayOff:	displayOff = settings->sleepWithDisplay; break;
				case power::Event::displayOn:	displayOff = false; break;
			}
			update ();
			return true;

		case WM_WTSSESSION_CHANGE:
			if ( wParam == WTS_SESSION_LOCK )
				locked = settings->sleepWithLock;
			else if ( wParam == WTS_SESSION_UNLOCK )
				locked = false;
			else
				return false;
			update ();
			return true;
	}

	return false;
}
//-----------------------------------------------------------------------------
