#pragma once

#include <mfapi.h>

#include <wil/resource.h>
#include <wil/com.h>

#include <Audioclient.h>
#include <audioclientactivationparams.h>

#include <vector>

#include "ProcessCapture.hpp"

//To Do:

//Class to store and handle an audio capture
//Class to represent a potential audio capture source
//Function to get all potential audio capture sources
//Function to create a handler from an audio capture source

//Class to collect and manage UI elements
//Basic UI prototype

class AudioManager
{
public:

	//necessary components for other things (like CaptureSourceStreams) to start audio loopback and audio output
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

	
};

int NameFromProcessID(DWORD pid, std::wstring& strOut);
void GetAllAudioSessionSources(std::vector<CaptureSource>& sources);