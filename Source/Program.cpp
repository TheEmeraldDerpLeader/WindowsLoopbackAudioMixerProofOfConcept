#include <iostream>
#include <string>

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include <Windows.h>
#include <Psapi.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <audioclientactivationparams.h>

#include <mfapi.h>

//To Do:

//Separate class to handle sfml window management and draw calls
//Class to collect and manage UI elements
//Basic UI prototype

//Windows API boilerplate
//Test the new API function (ActivateAudioInterfaceAsync)

void WindowsTest();

int main()
{
	std::cout << "Test!\n";
    WindowsTest();

    sf::RenderWindow window(sf::VideoMode({200, 200}), "SFML works!");
    sf::CircleShape shape(100.f);
    shape.setFillColor(sf::Color::Green);

    sf::SoundBuffer buf; //figure out how to create a .wav file in memory and play that instead
    buf.loadFromFile("test.ogg");
    sf::Sound sound(buf);
    sound.play();

    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
                window.close();
        }

        window.clear();
        window.draw(shape);
        window.display();
    }

	return 0;
}

class TestCallback : public IActivateAudioInterfaceCompletionHandler
{
public:
    HRESULT __stdcall ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation)
    { 
        // Check for a successful activation result
        HRESULT hrActivateResult = E_UNEXPECTED;
        IUnknown* punkAudioInterface;
        operation->GetActivateResult(&hrActivateResult, &punkAudioInterface);

        punkAudioInterface.copy_to(&m_AudioClient); //convert to Audio Output

        m_CaptureFormat.wFormatTag = WAVE_FORMAT_PCM; //set output format
        m_CaptureFormat.nChannels = 2;
        m_CaptureFormat.nSamplesPerSec = 44100;
        m_CaptureFormat.wBitsPerSample = 16;
        m_CaptureFormat.nBlockAlign = m_CaptureFormat.nChannels * m_CaptureFormat.wBitsPerSample / BITS_PER_BYTE;
        m_CaptureFormat.nAvgBytesPerSec = m_CaptureFormat.nSamplesPerSec * m_CaptureFormat.nBlockAlign;

        // Initialize the AudioClient in Shared Mode with the user specified buffer
        m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            200000,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
            &m_CaptureFormat,
            nullptr));

        // Get the maximum size of the AudioClient Buffer
        RETURN_IF_FAILED(m_AudioClient->GetBufferSize(&m_BufferFrames));

        // Get the capture client
        RETURN_IF_FAILED(m_AudioClient->GetService(IID_PPV_ARGS(&m_AudioCaptureClient)));

        // Create Async callback for sample events
        RETURN_IF_FAILED(MFCreateAsyncResult(nullptr, &m_xSampleReady, nullptr, &m_SampleReadyAsyncResult));

        // Tell the system which event handle it should signal when an audio buffer is ready to be processed by the client
        RETURN_IF_FAILED(m_AudioClient->SetEventHandle(m_SampleReadyEvent.get()));

        // Creates the WAV file.
        RETURN_IF_FAILED(CreateWAVFile());

        // Everything is ready.
        m_DeviceState = DeviceState::Initialized;

        return S_OK;
        return S_OK; 
    }
    
    //could use RunTimeClass<> inheritance to automatically define IUnknown functions, but not using it for simplicity
    ULONG __stdcall AddRef() { return S_OK; }
    ULONG __stdcall Release() { return S_OK; }
    template<typename Q> 
    HRESULT __stdcall QueryInterface(Q** pp) { pp = nullptr; return S_OK; }
    HRESULT __stdcall QueryInterface(const IID& riid, void** ppvObject) { ppvObject = nullptr;  return S_OK; }
};

void WindowsTest()
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
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice *pDevice = NULL;
    IAudioClient *pAudioClient = NULL;
    int count = 0;

    const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
    const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
    const IID IID_IAudioClient = __uuidof(IAudioClient);

    //for testing purposes just doing COM stuff here
    CoInitializeEx(0,COINIT::COINIT_APARTMENTTHREADED);

    //create a device enumerator object
    CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator,(void**)&pEnumerator);

    pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pDevice->Activate(IID_IAudioClient, CLSCTX_ALL,NULL, (void**)&pAudioClient);

    //setting up parameters for ActivateAudioInterfaceAsync call
    AUDIOCLIENT_ACTIVATION_PARAMS audioclientActivationParams = {};
    audioclientActivationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    audioclientActivationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    audioclientActivationParams.ProcessLoopbackParams.TargetProcessId = 0;// processId;

    PROPVARIANT activateParams = {};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(audioclientActivationParams);
    activateParams.blob.pBlobData = (BYTE*)&audioclientActivationParams;

    //the result of the operation, probably not used by this thread
    IActivateAudioInterfaceAsyncOperation* asyncOp;

    //see TestCallback definitions
    TestCallback aTest;
    ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &activateParams, &aTest, &asyncOp);

    //Wait for Async thread to have run before continuing
    
    if (pEnumerator != NULL) { pEnumerator->Release(); pEnumerator = NULL; }
    if (pDevice != NULL) { pDevice->Release(); pDevice = NULL; }
    if (pAudioClient != NULL) { pAudioClient->Release(); pAudioClient = NULL; }
    CoUninitialize();


    std::cout << "gurt: yo\n";
    //HRESULT err = ActivateAudioInterfaceAsync();
}
