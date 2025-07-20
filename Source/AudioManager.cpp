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

        /*WAVEFORMATEX m_CaptureFormat{};
        m_CaptureFormat.wFormatTag = WAVE_FORMAT_PCM; //set output format
        m_CaptureFormat.nChannels = 2;
        m_CaptureFormat.nSamplesPerSec = 44100;
        m_CaptureFormat.wBitsPerSample = 32;
        m_CaptureFormat.nBlockAlign = m_CaptureFormat.nChannels * m_CaptureFormat.wBitsPerSample / 8;
        m_CaptureFormat.nAvgBytesPerSec = m_CaptureFormat.nSamplesPerSec * m_CaptureFormat.nBlockAlign;*/

        // Initialize the AudioClient in Shared Mode with the user specified buffer
        //err = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, &m_CaptureFormat, nullptr);
        err = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, 0, caller.format, nullptr);

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
    return;

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

    //setup render client
    err = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    err = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL,NULL, (void**)&pAudioClient);
    pAudioClient->GetMixFormat(&format);
    err = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 200000, 0, format, nullptr);
    err = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&defaultRenderClient);

    

    //rewrite to output
    BYTE* dataWrite = nullptr;
    UINT32 bufSize = 0;
    pAudioClient->GetBufferSize(&bufSize);
    err = defaultRenderClient->GetBuffer(bufSize, &dataWrite);
    if (err.err == S_OK)
    {
        for (int i = 0; i < bufSize*4*format->nChannels; i++)
            dataWrite[i] = 0;
    }
    err = defaultRenderClient->ReleaseBuffer(bufSize,0);
    pAudioClient->Start();

    //setting up parameters for ActivateAudioInterfaceAsync call
    AUDIOCLIENT_ACTIVATION_PARAMS audioclientActivationParams = {};
    audioclientActivationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    audioclientActivationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    audioclientActivationParams.ProcessLoopbackParams.TargetProcessId = 8832;// processId;

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

    SessionEnumerationTest();

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

void AudioManager::SessionEnumerationTest()
{
    ErrorHandler err;
    std::cout << "\nSessionEnumerationTest:\n";
    
    //get default device
    wil::com_ptr<IMMDeviceEnumerator> pEnumerator;
    err = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),(void**)&pEnumerator);
    wil::com_ptr<IMMDevice> pDevice;
    err = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);

    //get session manager and enumerator
    wil::com_ptr<IAudioSessionManager2> sessionMan;
    err = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)(&sessionMan));
    wil::com_ptr<IAudioSessionEnumerator> sessionEnum;
    err = sessionMan->GetSessionEnumerator(&sessionEnum);
    int sessionCount;
    err = sessionEnum->GetCount(&sessionCount);
    std::cout << "Session Count: " << sessionCount << '\n';
    for (int i = 0; i < sessionCount; i++)
    {
        wil::com_ptr<IAudioSessionControl> session;
        wil::com_ptr<IAudioSessionControl2> sessionFull;
        err = sessionEnum->GetSession(i, &session);
        err = session->QueryInterface<IAudioSessionControl2>(&sessionFull);
        if (err.err == S_OK)
        {
            //wil::unique_cotaskmem_string name; //bruh
            //sessionFull->GetSessionInstanceIdentifier(&name);
            DWORD processID;
            sessionFull->GetProcessId(&processID);
            std::wstring name;
            if (NameFromProcessID(processID, name) == S_OK)
                std::wcout << "Session " << i << ": " << name << '\n';
        }
    }

    std::cout << '\n';
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
    err = m_AudioCaptureClient->GetBuffer(&data, &numFrames, &flags, nullptr, &start);
    //std::cout << start << '\n';

    //rewrite to output
    float* dataWrite = nullptr;
    err = defaultRenderClient->GetBuffer(numFrames, reinterpret_cast<BYTE**>(&dataWrite));
    if (err.err == S_OK)
    {
        for (int i = 0; i < numFrames*format->nChannels; i++)           
            dataWrite[i] = reinterpret_cast<float*>(data)[i];
    }

    err = m_AudioCaptureClient->ReleaseBuffer(numFrames);
    err = defaultRenderClient->ReleaseBuffer(numFrames,0);
    
    //if behind, clear output and get caught up
    err = m_AudioCaptureClient->GetNextPacketSize(&numFrames);
    if (numFrames != 0 && err.err == S_OK)
    {
        std::cout << "reset\n";
        err = m_AudioCaptureClient->GetBuffer(&data, &numFrames, &flags, nullptr, &start);

        //rewrite to output
        float* dataWrite = nullptr;
        err = defaultRenderClient->GetBuffer(numFrames, reinterpret_cast<BYTE**>(&dataWrite));
        if (err.err == S_OK)
        {
            pAudioClient->Stop();
            pAudioClient->Reset();
            pAudioClient->Start();

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

int NameFromProcessID(DWORD pid, std::wstring& strOut)
{
    wil::unique_handle process(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid));
    if (process == NULL)
    {
        strOut = L"Error: Invalid Process ID";
        return -1;
    }
    static int largestPath = 128; //largest path name ever recorded when this function has run
    std::vector<wchar_t> processName; processName.resize(largestPath);
    DWORD nameSize = 0;
    while (true)
    {
        nameSize = GetProcessImageFileNameW(process.get(), processName.data(), processName.size());
        if (nameSize == processName.size()) //recorded name took up entire name buffer, name might be longer
            processName.resize(nameSize*2);
        else //recorded name is fully within buffer
            break;
    }
    largestPath = processName.size();
    processName.resize(nameSize);
    std::wstring& out = strOut; out.clear();
    //just get executable name
    int index = 0;
    for (int i = nameSize-1; i >= 0; i--)
        if (processName[i] == '\\')
        {
            index = i+1;
            break;
        }
    for (int i = index; i < nameSize; i++)
        out.push_back(processName[i]);

    return S_OK;
}

void GetAllAudioSessionSources(std::vector<CaptureSource>& sources)
{
    sources.clear();

    ErrorHandler err;

    //get default device
    wil::com_ptr<IMMDeviceEnumerator> pEnumerator;
    err = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),(void**)&pEnumerator);
    wil::com_ptr<IMMDevice> pDevice;
    //err = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    
    //iterate through all active audio devices
    IMMDeviceCollection* devices;
    err = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
    UINT countUINT;
    devices->GetCount(&countUINT);
    for (int i = 0; i < countUINT; i++)
    {
        /* test code to get device name
        IMMDevice* testDevice;
        devices->Item(i, &testDevice);
        IPropertyStore* props;
        testDevice->OpenPropertyStore(STGM_READ, &props);
        PROPVARIANT val;
        props->GetValue(PKEY_Device_FriendlyName, &val);
        std::wcout << val.pwszVal << '\n';
        if (props != NULL) { props->Release(); props = NULL; }
        if (testDevice != NULL) { testDevice->Release(); testDevice = NULL; }*/

        //get device
        err = devices->Item(i, &pDevice);
        if (err.err != S_OK)
            continue;

        //get that audio session's device name and ID
        wil::com_ptr<IPropertyStore> props;
        pDevice->OpenPropertyStore(STGM_READ, &props);
        wil::unique_prop_variant val;
        props->GetValue(PKEY_Device_FriendlyName, &val);
        wil::unique_cotaskmem_string deviceIDPtr;
        pDevice->GetId(&deviceIDPtr);
        std::wstring deviceName = val.pwszVal;
        std::wstring deviceID = deviceIDPtr.get();

        //get session manager and enumerator
        wil::com_ptr<IAudioSessionManager2> sessionMan;
        err = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)(&sessionMan));
        wil::com_ptr<IAudioSessionEnumerator> sessionEnum;
        err = sessionMan->GetSessionEnumerator(&sessionEnum);
        int sessionCount;
        err = sessionEnum->GetCount(&sessionCount);

        for (int i = 0; i < sessionCount; i++)
        {
            wil::com_ptr<IAudioSessionControl> session;
            wil::com_ptr<IAudioSessionControl2> sessionFull;
            err = sessionEnum->GetSession(i, &session);
            err = session->QueryInterface<IAudioSessionControl2>(&sessionFull);
            if (err.err == S_OK)
            {
                //wil::unique_cotaskmem_string name; //bruh
                //sessionFull->GetSessionInstanceIdentifier(&name);
                DWORD processID;
                sessionFull->GetProcessId(&processID);
                std::wstring name;
                if (NameFromProcessID(processID, name) == S_OK)
                    sources.push_back(CaptureSource(name, processID, deviceID, deviceName));

            }
        }
    }
}
