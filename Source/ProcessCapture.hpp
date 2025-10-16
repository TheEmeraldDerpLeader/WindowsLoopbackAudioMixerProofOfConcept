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

#include <Helpers.hpp>

class CaptureSourceStream;
class CaptureSourceControl;
class CaptureSourceControlWatch;

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

class CaptureSourceControlWatch : public IAudioSessionEvents
{
public:

	std::atomic<unsigned int> refCount = 0;
	float volume = 0;

	HRESULT __stdcall OnChannelVolumeChanged(DWORD ChannelCount, float* NewChannelVolumeArray, DWORD ChangedChannel, LPCGUID EventContext) { return S_OK; }
	HRESULT __stdcall OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext) { return S_OK; }
	HRESULT __stdcall OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext) { return S_OK; }
	HRESULT __stdcall OnIconPathChanged(LPCWSTR NewIconPath, LPCGUID EventContext) { return S_OK; }
	HRESULT __stdcall OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) { std::cout << "Session Disconnected!"; return S_OK; } //this one never runs bruh
	HRESULT __stdcall OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext)
	{ 
		std::cout << "Volume changed: " << NewVolume;
		
		volume = NewVolume;

		return S_OK; 
	}
	HRESULT __stdcall OnStateChanged(AudioSessionState NewState) { return S_OK; }

	ULONG __stdcall AddRef() { refCount++; return refCount; }
	ULONG __stdcall Release() { refCount--; if (refCount == 0) {delete this; return 0;} return refCount; }
	template<typename Q> 
	HRESULT __stdcall QueryInterface(Q** pp) { pp = nullptr; return S_OK; }
	HRESULT __stdcall QueryInterface(const IID& riid, void** ppvObject) { ppvObject = nullptr;  return S_OK; }
};

class CaptureSourceControl
{
public:
	CaptureSource source;
	wil::com_ptr<CaptureSourceControlWatch> callback;

	wil::com_ptr<IAudioSessionControl2> session;
	wil::com_ptr<ISimpleAudioVolume> sessionVolume;
	wil::com_ptr<IAudioMeterInformation> sessionMeter;
	std::wstring& name() { return source.processName; }
	float _dummyVolume = -1;
	float& volume() { if (callback != nullptr) return callback->volume; else return _dummyVolume; } //volume normally lasts longer than this instance

	CaptureSourceControl(){}
	CaptureSourceControl(CaptureSource sourceCS) : source(sourceCS) {}
	CaptureSourceControl(wil::com_ptr<IAudioSessionControl2>& sessionRef);
	CaptureSourceControl(wil::com_ptr<IAudioSessionControl2>& sessionRef, ErrorHandler* errPtr); //nullptr to use local error handler
	CaptureSourceControl(wil::com_ptr<IAudioSessionControl2>& sessionRef, CaptureSource sourceCS);
	void UpdateDeviceInfo(AudioDeviceControl& device);

	CaptureSourceControl& operator=(CaptureSourceControl&& move);
	CaptureSourceControl(CaptureSourceControl&& move) { this->operator=(std::move(move)); }
	CaptureSourceControl& operator=(CaptureSourceControl& copy) = delete;
	CaptureSourceControl(CaptureSourceControl& copy) = delete;
	~CaptureSourceControl();
};


#include <chrono>

class CSessionNotifications: public IAudioSessionNotification
{
public:

	LONG m_cRefAll = 0;  

	PipeQ<CaptureSourceControl> newSessions;
	
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
		ErrorHandler err;

		std::cout << "New Session: ";

		wil::com_ptr<IAudioSessionControl2> sessionFull;
		err = pNewSession->QueryInterface<IAudioSessionControl2>(&sessionFull);
		CaptureSourceControl test = CaptureSourceControl(sessionFull,&err);

		std::wcout << test.name() << '\n';

		if (err.wasTripped == false)
			newSessions.push_back(CaptureSourceControl(sessionFull));

		return S_OK;
	}
};

int NameFromProcessID(DWORD pid, std::wstring& strOut);