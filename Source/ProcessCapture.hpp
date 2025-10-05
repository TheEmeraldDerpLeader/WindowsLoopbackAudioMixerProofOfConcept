#pragma once

#include <audiopolicy.h>
#include <mfapi.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <Audioclient.h>

#include <wil/com.h>
#include <wil/resource.h>

#include <iostream>
#include <vector>
#include <mutex>
#include <string>

class CaptureSourceStream;
class CaptureSourceControl;

class RCMutex
{
public:
	std::mutex mut;
	std::mutex modifySelf;
	int count = 0;

	void Decrement() { if (mut.try_lock() == true) mut.unlock(); else { std::cout << "decrementing locked mutex!"; abort(); } modifySelf.lock(); count--; if (count == 0) { modifySelf.unlock(); delete this; return; } modifySelf.unlock(); }
};
class RCMutexRef
{
public:
	RCMutex* ptr = nullptr;

	RCMutexRef() = default;
	//must ensure that Decrement isn't being called while this is running, otherwise RCMutex could delete itself and cause a deadlock (could add error check for that)
	RCMutexRef(RCMutex* mutPtr) { mutPtr->modifySelf.lock(); ptr = mutPtr; ptr->count++; ptr->modifySelf.unlock(); }
	RCMutexRef& operator=(RCMutex* mutPtr) { this->~RCMutexRef(); mutPtr->modifySelf.lock(); ptr = mutPtr; ptr->count++; ptr->modifySelf.unlock(); return *this; }
	~RCMutexRef() { if (ptr != nullptr) { ptr->Decrement(); ptr = nullptr; } }

	//rule of 5 setup
	RCMutexRef(const RCMutexRef& copyRef) { new(this) RCMutexRef(copyRef.ptr); }
	RCMutexRef(RCMutexRef&& moveRef) { ptr = moveRef.ptr; moveRef.ptr = nullptr; }
	//inplace new to call constructor, doesn't allocate memory
	RCMutexRef& operator=(const RCMutexRef& copyRef) { this->~RCMutexRef(); new(this) RCMutexRef(copyRef); return *this; }
	RCMutexRef& operator=(RCMutexRef&& moveRef) { this->~RCMutexRef(); new(this) RCMutexRef(std::move(moveRef)); return *this; }


	void Lock() { ptr->mut.lock(); }
	void Unlock() { ptr->mut.unlock(); }
	bool TryLock() { return ptr->mut.try_lock(); }
};

struct CaptureSource
{
public:
	std::wstring processName;
	DWORD processID = NULL;
	std::wstring deviceID;
	std::wstring deviceName;
	std::wstring sessionID;

	CaptureSource() {}
	CaptureSource(std::wstring processNameWString, DWORD processIDDWORD, std::wstring deviceIDWString, std::wstring deviceNameWString, std::wstring sessionIDWString)
		: processName(processNameWString), processID(processIDDWORD), deviceID(deviceIDWString), deviceName(deviceNameWString), sessionID(sessionIDWString) {}

	CaptureSourceStream GetStream();
	CaptureSourceControl GetControl();
};

//class allocated to wait for the windows callback, deletes itself once callback is recieved
class __CaptureSourceStreamCallback;
class __CaptureSourceStreamASyncCallback;
class CaptureSourceStream
{
public:
	CaptureSource source;

	//if not nullptr, this object is currently waiting for callback and can't be copied
	__CaptureSourceStreamCallback* _callback = nullptr;
	RCMutexRef _accessCallback;


	wil::com_ptr<IAudioCaptureClient> m_AudioCaptureClient;
	wil::com_ptr<IAudioClient> captureClientDevice;
	wil::unique_cotaskmem format; //WAVEFORMATEX*

	__CaptureSourceStreamASyncCallback* gotAudioFunction = nullptr;
	RCMutexRef _accessGotAudioFunction;

	wil::com_ptr<IAudioRenderClient> defaultAudioRenderClient;
	wil::com_ptr<IAudioClient> defaultAudioClient;
	bool isRendering = false;

	//rule of 5 setup
	CaptureSourceStream();
	CaptureSourceStream(const CaptureSourceStream& copyRef) = delete; //ASyncCallback isn't easily copyable
	CaptureSourceStream(CaptureSourceStream&& moveRef); //move assignment always works, even if waiting for callback
	//inplace new to call constructor, doesn't allocate memory
	CaptureSourceStream& operator=(const CaptureSourceStream& copyRef) = delete;//{ this->~CaptureSourceStream(); new(this) CaptureSourceStream(copyRef); }
	CaptureSourceStream& operator=(CaptureSourceStream&& moveRef) { this->~CaptureSourceStream(); new(this) CaptureSourceStream(std::move(moveRef)); return *this; }

	//also puts object into empty but usable state
	~CaptureSourceStream();


	void StartStream();
	void HandleAudioPacket();
};

class AudioDeviceControl
{
public:
	wil::com_ptr<IMMDevice> device;
	wil::com_ptr<IAudioEndpointVolume> deviceVolume;
	wil::com_ptr<IAudioMeterInformation> deviceMeter;
	std::wstring deviceName;
	std::wstring deviceID;
	float volume = 0;

	AudioDeviceControl() {}
	AudioDeviceControl(wil::com_ptr<IMMDevice>& deviceRef);
};

class CaptureSourceControl
{
public:
	CaptureSource source;

	wil::com_ptr<IAudioSessionControl2> session;
	wil::com_ptr<ISimpleAudioVolume> sessionVolume;
	wil::com_ptr<IAudioMeterInformation> sessionMeter;
	std::wstring& name() { return source.processName; }
	float volume = 0;

	CaptureSourceControl(){}
	CaptureSourceControl(CaptureSource sourceCS) : source(sourceCS) {}
	CaptureSourceControl(wil::com_ptr<IAudioSessionControl2>& sessionRef);
};

class CaptureSourceControlWatch : public IAudioSessionEvents
{
public:

	std::atomic<unsigned int> refCount = 0;

	HRESULT __stdcall OnChannelVolumeChanged(DWORD ChannelCount, float* NewChannelVolumeArray, DWORD ChangedChannel, LPCGUID EventContext) { return S_OK; }
	HRESULT __stdcall OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext) { return S_OK; }
	HRESULT __stdcall OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext) { return S_OK; }
	HRESULT __stdcall OnIconPathChanged(LPCWSTR NewIconPath, LPCGUID EventContext) { return S_OK; }
	HRESULT __stdcall OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) { std::cout << "Session Disconnected!"; return S_OK; } //this one never runs bruh
	HRESULT __stdcall OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext) { std::cout << "Volume changed: " << NewVolume; return S_OK; }
	HRESULT __stdcall OnStateChanged(AudioSessionState NewState) { return S_OK; }

	ULONG __stdcall AddRef() { refCount++; return refCount; }
	ULONG __stdcall Release() { refCount--; if (refCount == 0) {delete this; return 0;} return refCount; }
	template<typename Q> 
	HRESULT __stdcall QueryInterface(Q** pp) { pp = nullptr; return S_OK; }
	HRESULT __stdcall QueryInterface(const IID& riid, void** ppvObject) { ppvObject = nullptr;  return S_OK; }
};

#include <chrono>

class CSessionNotifications: public IAudioSessionNotification
{
public:

	LONG m_cRefAll = 0;  

	//if lock would be a compare and swap atomic action
	//if am tries to access, it locks its queue. this sees it is locked, so it sets lock and starts writing to other queue. When am finishes, it removes lock and sets curQueue to other (maybe use std::atomic)
	//if this tries to access, it locks its queue. Am sees this and sets curQueue to other then skips. This way this will start modifying other queue once its done. When am next checks this, it will first check the old queue and move items fromn it

	bool amLock = false; //whether am has curQueue locked
	bool thisLock = false; //whether this has curQueue locked
	int curQueue = 0; //which queue this will try to write to
	std::vector<CaptureSourceControl> sessionAddQueue1;
	std::vector<CaptureSourceControl> sessionAddQueue2;
	
	~CSessionNotifications() { }


	CSessionNotifications() {};

	HRESULT __stdcall QueryInterface(REFIID riid, void **ppv)  
	{    
		if (IID_IUnknown == riid)
		{
			AddRef();
			*ppv = (IUnknown*)this;
		}
		else if (__uuidof(IAudioSessionNotification) == riid)
		{
			AddRef();
			*ppv = (IAudioSessionNotification*)this;
		}
		else
		{
			*ppv = NULL;
			return E_NOINTERFACE;
		}
		return S_OK;
	}

	ULONG __stdcall AddRef()
	{
		return InterlockedIncrement(&m_cRefAll);
	}

	ULONG __stdcall Release() //ngl not implementing this is probs what causes the stream re-create problem.
	{
		ULONG ulRef = InterlockedDecrement(&m_cRefAll);
		if (0 == ulRef)
			delete this;
		return ulRef;
	}

	HRESULT OnSessionCreated(IAudioSessionControl *pNewSession) //guarentee that only one of this function runs at a time
	{ //why does it run twice ;_;
		std::cout << "New Session!";

		return S_OK;
	}
};

// audiopolicy.h
//Create IAudioSessionManager2
//Call GetSessionEnumerator to get IAudioSessionEnumerator
//Call RegisterSessionNotification to create a callback for when new sessions are created
//Use enumerator to get sessions
//Let user pick a session
//Use enumerator to get an IAudioSessionControl for that session
//Call RegisterAudioSessionNotification to get a IAudioSessionEvents for watch for session being removed?
//Call IAudioSessionManager::GetSimpleAudioVolume to get ISimpleAudioVolume for that session
//Program can now control the volume of that process