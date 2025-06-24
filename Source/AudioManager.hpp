#pragma once

#include <mfapi.h>

#include <wil/resource.h>
#include <wil/com.h>

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

	AudioManager();
	~AudioManager();

};