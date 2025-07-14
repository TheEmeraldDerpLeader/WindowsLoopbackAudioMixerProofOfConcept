#pragma once

#include <mfapi.h>

#include <wil/resource.h>
#include <wil/com.h>

#include <Audioclient.h>
#include <audioclientactivationparams.h>

//To Do:

//Separate class to handle sfml window management and draw calls
//Class to collect and manage UI elements
//Basic UI prototype

//Windows API boilerplate
//Test the new API function (ActivateAudioInterfaceAsync)

class AudioManager
{
public:
	wil::unique_event gotLoopback;
	wil::com_ptr<IAudioCaptureClient> m_AudioCaptureClient;
	wil::com_ptr<IAudioClient> captureClientDevice;

	wil::com_ptr<IAudioClient> pAudioClient;
	wil::com_ptr<IAudioRenderClient> defaultRenderClient;

	IMFAsyncCallback* gotAudioFunction;
	IMFAsyncResult* gotAudioAsyncID;

	HANDLE gotAudioEvent;

	WAVEFORMATEX* format;

	AudioManager();
	~AudioManager();

	void SessionEnumerationTest();

	void QueueLoopback();
	void HandleAudioPacket();

	int NameFromProcessID(DWORD pid, std::wstring& strOut);
};