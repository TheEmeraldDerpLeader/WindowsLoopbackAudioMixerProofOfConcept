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

	AudioManager();
	~AudioManager();
	
};

int NameFromProcessID(DWORD pid, std::wstring& strOut);
void GetAllAudioSessionSources(std::vector<CaptureSource>& sources);