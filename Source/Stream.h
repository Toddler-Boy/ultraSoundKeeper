#pragma once

#include <windows.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include "Config.h"
#include "Generator.h"

//-----------------------------------------------------------------------------

// One shared-mode, event-driven WASAPI render stream on one endpoint, fed on its own thread. The generator reads its
// parameters from settings every buffer. Made with heap::create; nothing runs a constructor otherwise

class Stream final : public IAudioSessionEvents
{
public:
	bool start ( IMMDevice* endpoint, const config::Settings* settings );
	void stop ();

	HRESULT STDMETHODCALLTYPE QueryInterface ( REFIID id, void** object ) override;
	ULONG STDMETHODCALLTYPE AddRef () override;
	ULONG STDMETHODCALLTYPE Release () override;

	HRESULT STDMETHODCALLTYPE OnDisplayNameChanged ( LPCWSTR, LPCGUID ) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnIconPathChanged ( LPCWSTR, LPCGUID ) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged ( float, BOOL, LPCGUID ) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged ( DWORD, float[], DWORD, LPCGUID ) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnGroupingParamChanged ( LPCGUID, LPCGUID ) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnStateChanged ( AudioSessionState ) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnSessionDisconnected ( AudioSessionDisconnectReason reason ) override;

private:
	// Anything but stopped is retried a second later: devices come back, exclusive-mode apps finish
	enum class Outcome
	{
		playing,
		stopped,
		retry
	};

	struct Session
	{
		IAudioClient*			client;
		IAudioRenderClient*		render;
		IAudioSessionControl*	control;
		bool					registered;
		UINT32					bufferFrames;
	};

	static DWORD WINAPI threadEntry ( void* stream );
	void run ();
	Outcome play ();
	Outcome open ( Session& session );
	Outcome loop ( const Session& session );
	bool fill ( const Session& session );
	void close ( Session& session );
	bool outputIs16Bit () const;

	IMMDevice*				endpoint;
	const config::Settings*	settings;
	HANDLE					thread;
	HANDLE					stopEvent;
	HANDLE					retryEvent;
	HANDLE					bufferEvent;
	generator::Format		format;
	generator::State		state;
};
//-----------------------------------------------------------------------------
