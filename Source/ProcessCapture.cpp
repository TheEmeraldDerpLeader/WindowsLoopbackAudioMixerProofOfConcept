#include "ProcessCapture.hpp"

#include "AudioManager.hpp"

#include <Windows.h>
#include <Psapi.h>
#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <Audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>

#include <Helpers.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

//based on code from https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/ApplicationLoopback

class AsyncCallbackSetup : public IActivateAudioInterfaceCompletionHandler
{
public:
    __CaptureSourceStreamCallback* callback = nullptr;

    AsyncCallbackSetup() = default;
    AsyncCallbackSetup(__CaptureSourceStreamCallback* callbackPtr) : callback(callbackPtr) {}
    //void Construct(AudioManager* amPtr, __CaptureSourceStreamCallback* callbackPtr) { am = amPtr, callback = callbackPtr; }

    HRESULT __stdcall ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation);

    //could use RunTimeClass<> inheritance to automatically define IUnknown functions, but not using it for simplicity
    ULONG __stdcall AddRef() { return S_OK; }
    ULONG __stdcall Release() { return S_OK; }
    template<typename Q> 
    HRESULT __stdcall QueryInterface(Q** pp) { pp = nullptr; return S_OK; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppvObject) { ppvObject = nullptr;  return S_OK; }
};

//class the will allocated to wait for the windows callback, deletes itself once callback is recieved
class __CaptureSourceStreamCallback
{
public:
    AsyncCallbackSetup async;
    RCMutexRef accessCallback;

    CaptureSourceStream* link;
};

HRESULT __stdcall AsyncCallbackSetup::ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation)
{
    callback->accessCallback.Lock(); //either wait for deletion or prevent stream from deleting link while this code is running
    CaptureSourceStream& stream = *callback->link;

    //stop execution if original stream object was destructed
    if (callback->link == nullptr)
    {
        //delete the link
        callback->accessCallback.Unlock();
        delete callback;

        return S_OK;
    }

    ErrorHandler err;

    // Check for a successful activation result
    HRESULT hrActivateResult = E_UNEXPECTED;
    IUnknown* punkAudioInterface;

    err = operation->GetActivateResult(&hrActivateResult, &punkAudioInterface);

    wil::com_ptr<IAudioClient> m_AudioClient;
    err = punkAudioInterface->QueryInterface<IAudioClient>(&m_AudioClient); //convert to Audio Output
    if (punkAudioInterface != NULL) { punkAudioInterface->Release(); punkAudioInterface = NULL; }

    /*WAVEFORMATEX m_CaptureFormat{};
    m_CaptureFormat.wFormatTag = WAVE_FORMAT_PCM; //set output format
    m_CaptureFormat.nChannels = 2;
    m_CaptureFormat.nSamplesPerSec = 44100;
    m_CaptureFormat.wBitsPerSample = 32;
    m_CaptureFormat.nBlockAlign = m_CaptureFormat.nChannels * m_CaptureFormat.wBitsPerSample / 8;
    m_CaptureFormat.nAvgBytesPerSec = m_CaptureFormat.nSamplesPerSec * m_CaptureFormat.nBlockAlign;*/

    // Initialize the AudioClient in Shared Mode with the user specified buffer
    //err = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, &m_CaptureFormat, nullptr);
    err = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, 0, reinterpret_cast<WAVEFORMATEX*>(stream.format.get()), nullptr);

    //figure out what's next, probs not render a wav, just see if we can output audio back to another audio endpoint, perhaps with delay. Also can test latency this way
    //also check if all of this can be done without async, maybe IMMDeviceEnumerator contains the process loopback device, this way we don't have to mess with virtual audio device string

    UINT32 m_BufferFrames = 0;

    // Get the maximum size of the AudioClient Buffer
    err = m_AudioClient->GetBufferSize(&m_BufferFrames);

    // Get the capture client
    err = m_AudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&(stream.m_AudioCaptureClient));
    stream.captureClientDevice = m_AudioClient;

    //start stream
    callback->link->StartStream();

    //delete the link
    callback->link->_callback = nullptr;
    callback->accessCallback.Unlock();
    delete callback;

    return S_OK;
}

CaptureSourceStream CaptureSource::GetStream()
{
    CaptureSourceStream out;
    ErrorHandler err;

    //setting up parameters for ActivateAudioInterfaceAsync call
    AUDIOCLIENT_ACTIVATION_PARAMS audioclientActivationParams = {};
    audioclientActivationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    audioclientActivationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    audioclientActivationParams.ProcessLoopbackParams.TargetProcessId = processID;

    PROPVARIANT activateParams = {};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(audioclientActivationParams);
    activateParams.blob.pBlobData = (BYTE*)&audioclientActivationParams;

    //the result of the operation, probably not used by this thread
    IActivateAudioInterfaceAsyncOperation* asyncOp;

    //setup callback link
    out._callback = new (__CaptureSourceStreamCallback);
    out._accessCallback = new RCMutex();
    out._callback->accessCallback = out._accessCallback;
    out._callback->link = &out;

    //see AsyncCallbackSetup definitions, this schedules ActivateCompleted to be called on a different thread
    out._callback->async = AsyncCallbackSetup(out._callback);
    AsyncCallbackSetup& aCallback = out._callback->async;
    err = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &activateParams, &aCallback, &asyncOp);

	return std::move(out);
}

CaptureSourceStream::CaptureSourceStream()
{
    ErrorHandler err;
    //setup render client
    {
        const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
        const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
        const IID IID_IAudioClient = __uuidof(IAudioClient);

        wil::com_ptr<IMMDeviceEnumerator> pEnumerator;
        wil::com_ptr<IMMDevice> pDevice;
        err = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator,(void**)&pEnumerator);
        err = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
        err = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL,NULL, (void**)&defaultAudioClient);
        defaultAudioClient->GetMixFormat(reinterpret_cast<WAVEFORMATEX**>(format.addressof()));
        err = defaultAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 200000, 0, reinterpret_cast<WAVEFORMATEX*>(format.get()), nullptr);
        err = defaultAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&defaultAudioRenderClient);
    }
}

//potentially fails if copyRef is waiting for callback, will be default initialized in that case
/*CaptureSourceStream::CaptureSourceStream(const CaptureSourceStream& copyRef)
{
    if (copyRef._callback == nullptr)
    {
        //_callback = copyRef._callback;
        //accessCallback; shouldn't be copied for this use case

        gotAudioFunction = copyRef.gotAudioFunction;
        gotAudioAsyncID = copyRef.gotAudioAsyncID;

        gotAudioEvent = copyRef.gotAudioEvent;
        
        am = moveRef.am;
    }
    else //if callback is not nullptr, then copyRef is still waiting for callback and cannot be copied
        *this = CaptureSourceStream();
}*/

class __CaptureSourceStreamASyncCallback : public IMFAsyncCallback
{
public:
    CaptureSourceStream* caller;
    HANDLE gotAudioEvent = NULL;
    RCMutexRef _canAccess;

    __CaptureSourceStreamASyncCallback() = delete;
    __CaptureSourceStreamASyncCallback(CaptureSourceStream* callerPtr) : caller(callerPtr) {}

    HRESULT __stdcall GetParameters(DWORD* pdwFlags, DWORD* pdwQueue) override
    {
        *pdwFlags = 0;
        *pdwQueue = MFASYNC_CALLBACK_QUEUE_STANDARD;
        return S_OK;
    }
    HRESULT __stdcall Invoke(IMFAsyncResult* pAsyncResult) override
    {
        _canAccess.Lock();
        if (caller != nullptr)
        {
            caller->HandleAudioPacket();
            _canAccess.Unlock();
        }
        else
        {
            _canAccess.Unlock();
            delete this;
        }
        return S_OK;
    }

    ~__CaptureSourceStreamASyncCallback()
    {
        if (gotAudioEvent != NULL)
            CloseHandle(gotAudioEvent);
    }

    //manual memory management
    ULONG __stdcall AddRef() { return S_OK; }
    ULONG __stdcall Release() { return S_OK; }

    //not gonna need to traverse inheritance tree
    template<typename Q> 
    HRESULT __stdcall QueryInterface(Q** pp) { pp = nullptr; return S_OK; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppvObject) { ppvObject = nullptr;  return S_OK; }
};

void CaptureSourceStream::StartStream()
{
    ErrorHandler err;

    _accessGotAudioFunction = new RCMutex();
    // Tell the system which event handle it should signal when an audio buffer is ready to be processed by the client
    gotAudioFunction = new __CaptureSourceStreamASyncCallback(this);
    gotAudioFunction->_canAccess = _accessGotAudioFunction;
    gotAudioFunction->gotAudioEvent = CreateEventA(NULL, false, false, NULL);
    err = captureClientDevice->SetEventHandle(gotAudioFunction->gotAudioEvent);
    err = MFCreateAsyncResult(NULL, gotAudioFunction, NULL, &gotAudioAsyncID);
    ResetEvent(gotAudioFunction->gotAudioEvent);
    err = MFPutWaitingWorkItem(gotAudioFunction->gotAudioEvent, 0, gotAudioAsyncID, NULL);

    captureClientDevice->Start();
}

void CaptureSourceStream::HandleAudioPacket()
{
    ErrorHandler err;

    if (isRendering == false)
    {
        isRendering = true;
        defaultAudioClient->Start();
    }

    //std::cout << "Handle packet\n";
    ResetEvent(gotAudioFunction->gotAudioEvent);
    err = MFPutWaitingWorkItem(gotAudioFunction->gotAudioEvent, 0, gotAudioAsyncID, NULL);

    BYTE* data;
    UINT32 numFrames;
    DWORD flags;
    UINT64 start;
    UINT64 end;
    err = m_AudioCaptureClient->GetBuffer(&data, &numFrames, &flags, nullptr, &start);
    
    WAVEFORMATEX* format = reinterpret_cast<WAVEFORMATEX*>(this->format.get());

    //rewrite to output
    float* dataWrite = nullptr;
    err = defaultAudioRenderClient->GetBuffer(numFrames, reinterpret_cast<BYTE**>(&dataWrite));
    if (err.err == S_OK)
    {
        for (int i = 0; i < numFrames*format->nChannels; i++)           
            dataWrite[i] = reinterpret_cast<float*>(data)[i];
    }

    err = m_AudioCaptureClient->ReleaseBuffer(numFrames);
    err = defaultAudioRenderClient->ReleaseBuffer(numFrames,0);

    //if behind, clear output and get caught up
    err = m_AudioCaptureClient->GetNextPacketSize(&numFrames);
    if (numFrames != 0 && err.err == S_OK)
    {
        std::cout << "reset\n";
        err = m_AudioCaptureClient->GetBuffer(&data, &numFrames, &flags, nullptr, &start);

        //rewrite to output
        float* dataWrite = nullptr;
        err = defaultAudioRenderClient->GetBuffer(numFrames, reinterpret_cast<BYTE**>(&dataWrite));
        if (err.err == S_OK)
        {
            defaultAudioClient->Stop();
            defaultAudioClient->Reset();
            defaultAudioClient->Start();

            /*UINT32 bufSize = 0;
            pAudioClient->GetBufferSize(&bufSize);
            err = defaultRenderClient->GetBuffer(bufSize, &dataWrite);
            for (int i = 0; i < bufSize*4*format->nChannels; i++)
            dataWrite[i] = 0;*/
            while (numFrames != 0 && err.err == S_OK)
            {
                err = m_AudioCaptureClient->GetBuffer(&data, &numFrames, &flags, nullptr, &start);
                err = m_AudioCaptureClient->ReleaseBuffer(numFrames);
                err = m_AudioCaptureClient->GetNextPacketSize(&numFrames);
            }
        }
    }
}

CaptureSourceStream::CaptureSourceStream(CaptureSourceStream&& moveRef)
{
    if (moveRef._accessCallback.ptr != nullptr)
        moveRef._accessCallback.Lock(); //lock callback before checking if it's nullptr
    if (moveRef._accessGotAudioFunction.ptr != nullptr)
        moveRef._accessGotAudioFunction.Lock();
    if (moveRef._callback != nullptr)
    {
        _callback = moveRef._callback; moveRef._callback = nullptr;
        _callback->link = this;
    }
    _accessCallback = std::move(moveRef._accessCallback);

    if (moveRef.gotAudioFunction != nullptr)
    {
        gotAudioFunction = moveRef.gotAudioFunction;
        gotAudioFunction->caller = this;
        gotAudioAsyncID = moveRef.gotAudioAsyncID;
    }

    m_AudioCaptureClient = std::move(moveRef.m_AudioCaptureClient);
    captureClientDevice = std::move(moveRef.captureClientDevice);
    format = std::move(moveRef.format);

    defaultAudioRenderClient = std::move(moveRef.defaultAudioRenderClient);
    defaultAudioClient = std::move(moveRef.defaultAudioClient);
    isRendering = moveRef.isRendering; moveRef.isRendering = false;

    _accessGotAudioFunction = std::move(moveRef._accessGotAudioFunction);

    if (_accessCallback.ptr != nullptr)
        _accessCallback.Unlock();
    if (_accessGotAudioFunction.ptr != nullptr)
        _accessGotAudioFunction.Unlock();
}

CaptureSourceStream::~CaptureSourceStream()
{
    //schedule callback to not setup callback when windows creates the callback
    if (_callback != nullptr)
    {
        //prevents link from deleting itself until this code finishes (specifically so accessCallback isn't invalidated while waiting for lock)
        if (_accessCallback.TryLock() == true)
            _callback->link = nullptr; //signal to AsyncCallbackSetup that this stream is destructed
        else
            _accessCallback.Lock(); //if deleteLink was already locked, then link is already in the process of deleting itself, just need to wait until its done
        _accessCallback.Unlock();
        _callback = nullptr;
    }
    if (_accessGotAudioFunction.ptr != nullptr)
    {
        _accessGotAudioFunction.Lock();
        if (gotAudioFunction != nullptr)
            gotAudioFunction->caller = nullptr;
        gotAudioFunction = nullptr;
        _accessGotAudioFunction.Unlock();
    }
    if (isRendering == true)
    {
        isRendering = false;
        defaultAudioClient->Stop();
    }
}