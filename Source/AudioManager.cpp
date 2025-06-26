#include "AudioManager.hpp"

#include <Windows.h>
#include <Psapi.h>
#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <Audioclient.h>
#include <audioclientactivationparams.h>

#include <Helpers.hpp>

#include <iostream>
#include <string>
#include <thread>

bool terminateTest = false; //semiphor for uninitializing COM

//modified code from https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/ApplicationLoopback
class TestCallback : public IActivateAudioInterfaceCompletionHandler
{
public:
    AudioManager& caller;

    TestCallback() = delete;
    TestCallback(AudioManager& callerRef) : caller(callerRef) {}

    HRESULT __stdcall ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation)
    {
        ErrorHandler err;
        
        // Check for a successful activation result
        HRESULT hrActivateResult = E_UNEXPECTED;
        IUnknown* punkAudioInterface;

        err = operation->GetActivateResult(&hrActivateResult, &punkAudioInterface);

        wil::com_ptr<IAudioClient> m_AudioClient;
        err = punkAudioInterface->QueryInterface<IAudioClient>(&m_AudioClient); //convert to Audio Output
        if (punkAudioInterface != NULL) { punkAudioInterface->Release(); punkAudioInterface = NULL; }

        WAVEFORMATEX m_CaptureFormat{};
        m_CaptureFormat.wFormatTag = WAVE_FORMAT_PCM; //set output format
        m_CaptureFormat.nChannels = 2;
        m_CaptureFormat.nSamplesPerSec = 44100;
        m_CaptureFormat.wBitsPerSample = 16;
        m_CaptureFormat.nBlockAlign = m_CaptureFormat.nChannels * m_CaptureFormat.wBitsPerSample / 8;
        m_CaptureFormat.nAvgBytesPerSec = m_CaptureFormat.nSamplesPerSec * m_CaptureFormat.nBlockAlign;

        // Initialize the AudioClient in Shared Mode with the user specified buffer
        err = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, &m_CaptureFormat, nullptr);

        //figure out what's next, probs not render a wav, just see if we can output audio back to another audio endpoint, perhaps with delay. Also can test latency this way
        //also check if all of this can be done without async, maybe IMMDeviceEnumerator contains the process loopback device, this way we don't have to mess with virtual audio device string

        UINT32 m_BufferFrames = 0;

        // Get the maximum size of the AudioClient Buffer
        err = m_AudioClient->GetBufferSize(&m_BufferFrames);

        // Get the capture client
        err = m_AudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&(caller.m_AudioCaptureClient));
        caller.captureClientDevice = m_AudioClient;

        terminateTest = true;
        return S_OK;
    }

    //could use RunTimeClass<> inheritance to automatically define IUnknown functions, but not using it for simplicity
    ULONG __stdcall AddRef() { return S_OK; }
    ULONG __stdcall Release() { return S_OK; }
    template<typename Q> 
    HRESULT __stdcall QueryInterface(Q** pp) { pp = nullptr; return S_OK; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppvObject) { ppvObject = nullptr;  return S_OK; }
};

class AudioManagerASyncCallback : public IMFAsyncCallback
{
public:
    AudioManager* caller;

    AudioManagerASyncCallback() = delete;
    AudioManagerASyncCallback(AudioManager* callerPtr) : caller(callerPtr) {}

    HRESULT __stdcall GetParameters(DWORD* pdwFlags, DWORD* pdwQueue) override
    {
        *pdwFlags = 0;
        *pdwQueue = MFASYNC_CALLBACK_QUEUE_STANDARD;
        return S_OK;
    }
    HRESULT __stdcall Invoke(IMFAsyncResult* pAsyncResult) override
    {
        caller->HandleAudioPacket();
        return S_OK;
    }

    //manual memory management
    ULONG __stdcall AddRef() { return S_OK; }
    ULONG __stdcall Release() { return S_OK; }

    //not gonna need to traverse inheritance tree
    template<typename Q> 
    HRESULT __stdcall QueryInterface(Q** pp) { pp = nullptr; return S_OK; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppvObject) { ppvObject = nullptr;  return S_OK; }
};

AudioManager::AudioManager()
{
    //basic Windows API test
    DWORD processes[2048];
    DWORD retSize;
    EnumProcesses(processes, sizeof(DWORD)*2048, &retSize);
    for (int i = 0; i < retSize/sizeof(DWORD); i++)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processes[i]);
        if (process != NULL)
        {
            char processName[2048];
            DWORD nameSize = GetProcessImageFileNameA(process, processName, 2048);
            std::string pName;

            //just get executable name
            int index = 0;
            for (int i = nameSize-1; i >= 0; i--)
                if (processName[i] == '\\')
                {
                    index = i+1;
                    break;
                }
            for (int i = index; i < nameSize; i++)
                pName.push_back(processName[i]);

            //std::cout << pName << '\n';
            CloseHandle(process);
        }
    }

    //Get audio devices
    //use cocreateinstance to get an audio device enumerator
    HRESULT hr;
    wil::com_ptr<IMMDeviceEnumerator> pEnumerator;
    wil::com_ptr<IMMDevice> pDevice;
    wil::com_ptr<IAudioClient> pAudioClient;
    int count = 0;

    const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
    const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
    const IID IID_IAudioClient = __uuidof(IAudioClient);

    ErrorHandler err;

    //create a device enumerator object
    err = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator,(void**)&pEnumerator);

    IMMDeviceCollection* devices;
    pEnumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_NOTPRESENT | DEVICE_STATE_UNPLUGGED, &devices);
    UINT countUINT;
    devices->GetCount(&countUINT);
    for (int i = 0; i < countUINT; i++)
    {
        IMMDevice* testDevice;
        devices->Item(i, &testDevice);
        IPropertyStore* props;
        testDevice->OpenPropertyStore(STGM_READ, &props);
        PROPVARIANT val;
        props->GetValue(PKEY_Device_FriendlyName, &val);
        std::wcout << val.pwszVal << '\n';
        if (props != NULL) { props->Release(); props = NULL; }
        if (testDevice != NULL) { testDevice->Release(); testDevice = NULL; }

    }

    if (devices != NULL) { devices->Release(); devices = NULL; }

    err = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    err = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL,NULL, (void**)&pAudioClient);


    //setting up parameters for ActivateAudioInterfaceAsync call
    AUDIOCLIENT_ACTIVATION_PARAMS audioclientActivationParams = {};
    audioclientActivationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    audioclientActivationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    audioclientActivationParams.ProcessLoopbackParams.TargetProcessId = 4068;// processId;

    PROPVARIANT activateParams = {};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(audioclientActivationParams);
    activateParams.blob.pBlobData = (BYTE*)&audioclientActivationParams;

    //the result of the operation, probably not used by this thread
    IActivateAudioInterfaceAsyncOperation* asyncOp;

    //see TestCallback definitions
    TestCallback aTest(*this);
    err = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &activateParams, &aTest, &asyncOp);

    //Wait for Async thread to have run before continuing
    while (terminateTest == false)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Tell the system which event handle it should signal when an audio buffer is ready to be processed by the client
    gotAudioEvent = CreateEventA(NULL, false, false, NULL);
    err = captureClientDevice->SetEventHandle(gotAudioEvent);
    gotAudioFunction = new AudioManagerASyncCallback(this);
    err = MFCreateAsyncResult(NULL, gotAudioFunction, NULL, &gotAudioAsyncID);
    QueueLoopback();

    captureClientDevice->Start();

    std::cout << "end test\n";
    //HRESULT err = ActivateAudioInterfaceAsync();
}

AudioManager::~AudioManager()
{
    //cancel work item
    if (gotAudioFunction != nullptr)
        delete gotAudioFunction;

    if (gotAudioEvent != NULL)
        CloseHandle(gotAudioEvent);
}

void AudioManager::QueueLoopback()
{
    ErrorHandler err;

    ResetEvent(gotAudioEvent);
    err = MFPutWaitingWorkItem(gotAudioEvent, 0, gotAudioAsyncID, NULL);
}

void AudioManager::HandleAudioPacket()
{
    ErrorHandler err;

    //std::cout << "Handle packet\n";
    QueueLoopback();
    BYTE* data;
    UINT32 numFrames;
    DWORD flags;
    UINT64 start;
    UINT64 end;
    err = m_AudioCaptureClient->GetBuffer(&data, &numFrames, &flags, &start, &end);
    std::cout << "Packet size = " << numFrames << '\n';
    er //todo: check that this is actually reading audio, have a way for the callback to not run if AudioManager is destructed + callback destructs itself? 
}