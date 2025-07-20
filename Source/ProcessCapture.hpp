#pragma once

#include <mfapi.h>

#include <wil/resource.h>
#include <wil/com.h>

#include <string>
#include <mutex>


class RCMutex
{
public:
	std::mutex mut;
	std::mutex modifySelf;
	int count = 0;

	void Decrement() { modifySelf.lock(); count--; if (count == 0) delete this; }
};
class RCMutexRef
{
public:
	RCMutex* ptr = nullptr;

	RCMutexRef() = default;
	//must ensure that Decrement isn't being called while this is running, otherwise RCMutex could delete itself and cause a deadlock (could add error check for that)
	RCMutexRef(RCMutex* mutPtr) { mutPtr->modifySelf.lock(); ptr = mutPtr; ptr->count++; ptr->modifySelf.unlock(); }
	RCMutexRef& operator=(RCMutex* mutPtr) { this->~RCMutexRef(); mutPtr->modifySelf.lock(); ptr = mutPtr; ptr->count++; ptr->modifySelf.unlock();}
	~RCMutexRef() { if (ptr != nullptr) { ptr->Decrement(); ptr = nullptr; } }

	//rule of 5 setup
	RCMutexRef(const RCMutexRef& copyRef) { new(this) RCMutexRef(copyRef.ptr); }
	RCMutexRef(RCMutexRef&& moveRef) { ptr = moveRef.ptr; moveRef.ptr = nullptr; }
	//inplace new to call constructor, doesn't allocate memory
	RCMutexRef& operator=(const RCMutexRef& copyRef) { this->~RCMutexRef(); new(this) RCMutexRef(copyRef); }
	RCMutexRef& operator=(RCMutexRef&& moveRef) { this->~RCMutexRef(); new(this) RCMutexRef(moveRef); }


	void Lock() { ptr->mut.lock(); }
	void Unlock() { ptr->mut.unlock(); }
	bool TryLock() { return ptr->mut.try_lock(); }
};

//class the will allocated to wait for the windows callback, deletes itself once callback is recieved
class __CaptureSourceStreamCallback;
class CaptureSourceStream
{
public:
	//if not nullptr, this object is currently waiting for callback and can't be copied
	__CaptureSourceStreamCallback* _callback = nullptr;
	RCMutexRef _accessCallback;


	wil::com_ptr<IAudioCaptureClient> m_AudioCaptureClient;
	wil::com_ptr<IAudioClient> captureClientDevice;
	wil::unique_cotaskmem format;

	IMFAsyncCallback* gotAudioFunction = nullptr;
	IMFAsyncResult* gotAudioAsyncID = nullptr;

	HANDLE gotAudioEvent = NULL;

	//rule of 5 setup
	CaptureSourceStream() = default;
	CaptureSourceStream(const CaptureSourceStream& copyRef); //potentially fails if copyRef is waiting for callback, will be default initialized in that case
	CaptureSourceStream(CaptureSourceStream&& moveRef); //move assignment always works, even if waiting for callback
	//inplace new to call constructor, doesn't allocate memory
	CaptureSourceStream& operator=(const CaptureSourceStream& copyRef) { this->~CaptureSourceStream(); new(this) CaptureSourceStream(copyRef); } //potentially fails if copyRef is waiting for callback, will be default initialized in that case
	CaptureSourceStream& operator=(CaptureSourceStream&& moveRef) { this->~CaptureSourceStream(); new(this) CaptureSourceStream(moveRef); } 

	//also puts object into empty but usable state
	~CaptureSourceStream();


	void StartStream();
};
class CaptureSource
{
public:
	std::wstring processName;
	DWORD processID;
	std::wstring deviceID;
	std::wstring deviceName;
	//sessionID

	CaptureSource(std::wstring processNameWString, DWORD processIDDWORD, std::wstring deviceIDWString, std::wstring deviceNameWString)
		: processName(processNameWString), processID(processIDDWORD), deviceID(deviceIDWString), deviceName(deviceNameWString) {}

	CaptureSourceStream GetStream(AudioManager& am);
};