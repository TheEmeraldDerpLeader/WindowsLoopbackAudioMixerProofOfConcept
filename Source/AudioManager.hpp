#pragma once

#include <mfapi.h>

#include <wil/resource.h>
#include <wil/com.h>

#include <Audioclient.h>
#include <audioclientactivationparams.h>

#include <vector>

#include "ProcessCapture.hpp"

class AudioManager
{
public:

	AudioManager();
	~AudioManager();

	std::vector<AudioDeviceControl> devices;
	std::vector<wil::com_ptr<IAudioSessionManager2>> sessionManagers;
	std::vector<std::vector<CaptureSourceControl>> sessions;
	std::vector<std::vector<wil::com_ptr<CaptureSourceControlWatch>>> sessionWatches;
	std::vector<wil::com_ptr<CSessionNotifications>> newSessionWatch;
	//IMMDeviceNotification, register on IMMEnumerator doesn't touch its ref count, so no need for pointer
	//Watches does have AddRef called, doc doesn't say if Notifications has AddRef called -_-

	void ResetSessions();

	std::vector<std::vector<float>> GetVolumes();
	std::vector<float> GetSessionVolumes(int deviceIndex);

	float GetSessionVolume(int deviceIndex, int sessionIndex);
	float GetDeviceVolume(int deviceIndex);

	void SetDirectSessionVolume(int deviceIndex, int sessionIndex, float val);
	void SetSessionVolume(int deviceIndex, int sessionIndex, float val);
	void OffsetSessionVolume(int deviceIndex, int sessionIndex, float offset);
	void SetAllSessionVolumes(int deviceIndex, float val);
	void SetDeviceVolume(int deviceIndex, float val);

	void HandleNewSessions();
};

void GetAllAudioSessionSources(std::vector<CaptureSource>& sources);