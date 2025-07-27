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

AudioManager::AudioManager()
{
    return;
}

AudioManager::~AudioManager()
{
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
