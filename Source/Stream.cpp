#include <array>

#include <windows.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <objbase.h>
#include <propidl.h>

#include "Stream.h"

//-----------------------------------------------------------------------------

namespace
{
	constexpr auto	retryMilliseconds = DWORD ( 1000 );
}
//-----------------------------------------------------------------------------

bool Stream::start ( IMMDevice* device, const config::Settings* live )
{
	endpoint = device;
	endpoint->AddRef ();
	settings = live;
	state = {};

	stopEvent = CreateEventW ( nullptr, TRUE, FALSE, nullptr );
	retryEvent = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
	bufferEvent = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
	thread = stopEvent != nullptr && retryEvent != nullptr && bufferEvent != nullptr ? CreateThread ( nullptr, 0, threadEntry, this, 0, nullptr ) : nullptr;

	if ( thread == nullptr )
		stop ();

	return thread != nullptr;
}

void Stream::stop ()
{
	if ( thread != nullptr )
	{
		SetEvent ( stopEvent );
		WaitForSingleObject ( thread, INFINITE );
		CloseHandle ( thread );
	}

	for ( auto* handle : { &thread, &stopEvent, &retryEvent, &bufferEvent } )
	{
		if ( *handle != nullptr )
			CloseHandle ( *handle );
		*handle = nullptr;
	}

	if ( endpoint != nullptr )
		endpoint->Release ();
	endpoint = nullptr;
}

HRESULT Stream::QueryInterface ( REFIID id, void** object )
{
	if ( object == nullptr )
		return E_POINTER;

	if ( id != IID_IUnknown && id != __uuidof ( IAudioSessionEvents ) )
	{
		*object = nullptr;
		return E_NOINTERFACE;
	}

	*object = static_cast<IAudioSessionEvents*> ( this );
	return S_OK;
}

// The keeper owns the stream and unregisters it before destroying it, so there is nothing to count
ULONG Stream::AddRef ()
{
	return 1;
}

ULONG Stream::Release ()
{
	return 1;
}

// Whatever the reason (format change, exclusive-mode app, device gone), reopening is the right answer; a device that
// is gone for good gets the keeper's restart on top
HRESULT Stream::OnSessionDisconnected ( AudioSessionDisconnectReason )
{
	SetEvent ( retryEvent );
	return S_OK;
}
//-----------------------------------------------------------------------------

DWORD WINAPI Stream::threadEntry ( void* stream )
{
	static_cast<Stream*> ( stream )->run ();
	return 0;
}

void Stream::run ()
{
	if ( FAILED ( CoInitializeEx ( nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE ) ) )
		return;

	auto		task = DWORD ( 0 );
	const auto	mmcss = AvSetMmThreadCharacteristicsW ( L"Audio", &task );

	for ( ;; )
	{
		if ( play () == Outcome::stopped )
			break;

		if ( WaitForSingleObject ( stopEvent, retryMilliseconds ) == WAIT_OBJECT_0 )
			break;
	}

	if ( mmcss != nullptr )
		AvRevertMmThreadCharacteristics ( mmcss );

	CoUninitialize ();
}

Stream::Outcome Stream::play ()
{
	auto	session = Session {};
	auto	outcome = open ( session );
	if ( outcome == Outcome::playing )
		outcome = loop ( session );

	close ( session );
	return outcome;
}

Stream::Outcome Stream::open ( Session& session )
{
	if ( FAILED ( endpoint->Activate ( __uuidof ( IAudioClient ), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**> ( &session.client ) ) ) )
		return Outcome::retry;

	format.output16Bit = outputIs16Bit ();

	// The shared-mode mix is float32 on every Windows since Vista, anything else is not worth handling
	auto*	mix = static_cast<WAVEFORMATEX*> ( nullptr );
	if ( FAILED ( session.client->GetMixFormat ( &mix ) ) )
		return Outcome::retry;

	const auto	isFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
						|| ( mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*> ( mix )->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT );
	format.sampleRate = int ( mix->nSamplesPerSec );
	format.channels = mix->nChannels;

	// A zero buffer request gets the engine's smallest, so a changed setting is heard within a period or two
	const auto	result = isFloat && mix->wBitsPerSample == 32
						 ? session.client->Initialize ( AUDCLNT_SHAREMODE_SHARED,
														AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
														0, 0, mix, nullptr )
						 : E_FAIL;
	CoTaskMemFree ( mix );

	// An exclusive-mode app holds the device (ASIO reports busy instead); it gets retried until it lets go
	if ( result == AUDCLNT_E_DEVICE_IN_USE || result == HRESULT_FROM_WIN32 ( ERROR_BUSY ) )
		return Outcome::retry;
	if ( FAILED ( result ) )
		return Outcome::retry;

	if ( FAILED ( session.client->GetBufferSize ( &session.bufferFrames ) )
		 || FAILED ( session.client->SetEventHandle ( bufferEvent ) )
		 || FAILED ( session.client->GetService ( IID_PPV_ARGS ( &session.render ) ) )
		 || FAILED ( session.client->GetService ( IID_PPV_ARGS ( &session.control ) ) ) )
		return Outcome::retry;

	session.registered = SUCCEEDED ( session.control->RegisterAudioSessionNotification ( this ) );

	if ( ! fill ( session ) || FAILED ( session.client->Start () ) )
		return Outcome::retry;

	return Outcome::playing;
}

// The engine signals bufferEvent each period that frees space; the timeout only covers a stalled engine.
// Open only never feeds the stream, that loop waits for stop or retry alone
Stream::Outcome Stream::loop ( const Session& session )
{
	const auto	handles = std::array { stopEvent, retryEvent, bufferEvent };
	const auto	feeding = settings->mode != config::Mode::openOnly;
	const auto	timeout = DWORD ( session.bufferFrames * 2000ull / format.sampleRate );

	for ( ;; )
	{
		const auto	result = WaitForMultipleObjects ( feeding ? 3 : 2, handles.data (), FALSE, feeding ? timeout : INFINITE );
		if ( result == WAIT_OBJECT_0 )
			return Outcome::stopped;
		if ( result == WAIT_OBJECT_0 + 1 || result == WAIT_FAILED )
			return Outcome::retry;

		if ( ! fill ( session ) )
			return Outcome::retry;
	}
}

bool Stream::fill ( const Session& session )
{
	auto	padding = UINT32 ( 0 );
	if ( FAILED ( session.client->GetCurrentPadding ( &padding ) ) )
		return false;

	const auto	frames = session.bufferFrames - padding;
	if ( frames == 0 )
		return true;

	auto*	data = static_cast<BYTE*> ( nullptr );
	if ( FAILED ( session.render->GetBuffer ( frames, &data ) ) )
		return false;

	const auto	written = generator::render ( state, *settings, format, reinterpret_cast<float*> ( data ), int ( frames ) );
	return SUCCEEDED ( session.render->ReleaseBuffer ( frames, written ? 0 : AUDCLNT_BUFFERFLAGS_SILENT ) );
}

void Stream::close ( Session& session )
{
	if ( session.client != nullptr )
		session.client->Stop ();
	if ( session.registered )
		session.control->UnregisterAudioSessionNotification ( this );

	for ( auto* object : { static_cast<IUnknown*> ( session.control ), static_cast<IUnknown*> ( session.render ), static_cast<IUnknown*> ( session.client ) } )
		if ( object != nullptr )
			object->Release ();

	session = {};
}

// WASAPI reports 24-bit outputs as 32 and Dolby passthrough as not PCM at all; only true 16-bit outputs need the larger step
bool Stream::outputIs16Bit () const
{
	auto*	store = static_cast<IPropertyStore*> ( nullptr );
	if ( FAILED ( endpoint->OpenPropertyStore ( STGM_READ, &store ) ) )
		return false;

	auto	value = PROPVARIANT {};
	auto	is16Bit = false;
	if ( SUCCEEDED ( store->GetValue ( PKEY_AudioEngine_DeviceFormat, &value ) ) && value.vt == VT_BLOB )
	{
		const auto*	output = reinterpret_cast<const WAVEFORMATEX*> ( value.blob.pBlobData );
		const auto	isPcm = output->wFormatTag == WAVE_FORMAT_PCM
							|| ( output->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<const WAVEFORMATEXTENSIBLE*> ( output )->SubFormat == KSDATAFORMAT_SUBTYPE_PCM );
		is16Bit = isPcm && output->wBitsPerSample == 16;
	}

	PropVariantClear ( &value );
	store->Release ();
	return is16Bit;
}
//-----------------------------------------------------------------------------
