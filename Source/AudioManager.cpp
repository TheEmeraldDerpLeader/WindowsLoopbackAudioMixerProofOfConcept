#include "AudioManager.hpp"

#include <Windows.h>
#include <Psapi.h>
#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <Audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <endpointvolume.h>


#include <Helpers.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <vector>

AudioManager::AudioManager()
{
    ResetSessions();
    return;
}

AudioManager::~AudioManager()
{
    ErrorHandler err;
    //for (int i = 0; i < sessionWatches.size(); i++)
    //    for (int j = 0; j < sessionWatches[i].size(); j++)
    //        sessionWatches[i][j]->Release();
    //for (int i = 0; i < newSessionWatch.size(); i++)
    //    if (newSessionWatch[i] != nullptr)
    //        newSessionWatch[i]->Release();
    for (int i = 0; i < sessionManagers.size(); i++)
        err = sessionManagers[i]->UnregisterSessionNotification(newSessionWatch[i].get());
    for (int i = 0; i < sessionWatches.size(); i++)
        for (int j = 0; j < sessionWatches[i].size(); j++)
            err = sessions[i][j].session->UnregisterAudioSessionNotification(sessionWatches[i][j].get());
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
        err = pDevice->OpenPropertyStore(STGM_READ, &props);
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
                wil::unique_cotaskmem_string sessionIDPtr;
                sessionFull->GetSessionInstanceIdentifier(&sessionIDPtr);
                if (NameFromProcessID(processID, name) == S_OK)
                    sources.push_back(CaptureSource(name, processID, deviceID, deviceName, sessionIDPtr.get()));

            }
        }
    }
}

void AudioManager::ResetSessions()
{
    ErrorHandler err;

    for (int i = 0; i < sessionManagers.size(); i++)
        err = sessionManagers[i]->UnregisterSessionNotification(newSessionWatch[i].get());
    for (int i = 0; i < sessionWatches.size(); i++)
        for (int j = 0; j < sessionWatches[i].size(); j++)
            err = sessions[i][j].session->UnregisterAudioSessionNotification(sessionWatches[i][j].get());

    sessions.clear();
    //for (int i = 0; i < sessionWatches.size(); i++)
    //    for (int j = 0; j < sessionWatches[i].size(); j++)
    //        sessionWatches[i][j]->Release();
    sessionManagers.clear();
    sessionWatches.clear();
    //for (int i = 0; i < newSessionWatch.size(); i++)
    //    if (newSessionWatch[i] != nullptr)
    //        newSessionWatch[i]->Release();
    newSessionWatch.clear();
    devices.clear();


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

        //get device
        err = devices->Item(i, &pDevice);
        if (err.err != S_OK)
            continue;

        

        //get that audio session's device name and ID
        wil::com_ptr<IPropertyStore> props;
        err = pDevice->OpenPropertyStore(STGM_READ, &props);
        wil::unique_prop_variant val;
        props->GetValue(PKEY_Device_FriendlyName, &val);
        wil::unique_cotaskmem_string deviceIDPtr;
        pDevice->GetId(&deviceIDPtr);
        std::wstring deviceName = val.pwszVal;
        std::wstring deviceID = deviceIDPtr.get();

        //get session manager and enumerator
        wil::com_ptr<IAudioSessionManager2> sessionMan;
        err = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)(&sessionMan));
        sessionManagers.push_back(sessionMan);
        wil::com_ptr<IAudioSessionEnumerator> sessionEnum;
        err = sessionMan->GetSessionEnumerator(&sessionEnum);
        int sessionCount;
        err = sessionEnum->GetCount(&sessionCount);

        this->devices.push_back(AudioDeviceControl(pDevice));

        newSessionWatch.push_back(new CSessionNotifications()); //assigning ptr calls AddRef
        err = sessionMan->RegisterSessionNotification(newSessionWatch.back().get());

        sessions.push_back(std::vector<CaptureSourceControl>());
        sessions.back().reserve(sessionCount);
        std::vector<CaptureSourceControl>& sessionsCur = sessions.back();
        

        sessionWatches.push_back(std::vector<wil::com_ptr<CaptureSourceControlWatch>>());
        sessionWatches.back().reserve(sessionCount);
        std::vector<wil::com_ptr<CaptureSourceControlWatch>>& sessionWatchCur = sessionWatches.back();

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
                wil::unique_cotaskmem_string sessionIDPtr;
                err = sessionFull->GetSessionInstanceIdentifier(&sessionIDPtr);
                if (NameFromProcessID(processID, name) == S_OK)
                {
                    CaptureSource c = CaptureSource(name, processID, deviceID, deviceName, sessionIDPtr.get());

                    wil::com_ptr<IAudioSessionControl> session;
                    wil::com_ptr<IAudioSessionControl2> sessionFull;
                    err = sessionEnum->GetSession(i, &session);
                    err = session->QueryInterface<IAudioSessionControl2>(&sessionFull);

                    sessionsCur.push_back(CaptureSourceControl(sessionFull));

                    sessionWatchCur.push_back(new CaptureSourceControlWatch());
                    err = sessionFull->RegisterAudioSessionNotification(sessionWatchCur.back().get());

                    sessionsCur.back().source = std::move(c);
                }

            }
        }
    }
}

std::vector<std::vector<float>> AudioManager::GetVolumes()
{
    std::vector<std::vector<float>> out;

    out.resize(sessions.size());
    for (int i = 0; i < out.size(); i++)
    {
        out[i].resize(sessions[i].size());
        for (int j = 0; j < out[i].size(); j++)
        {
            HRESULT err = sessions[i][j].sessionVolume->GetMasterVolume(&(out[i][j]));
            if (err != S_OK)
                out[i][j] = -1;
        }
    }


    return out;
}

std::vector<float> AudioManager::GetSessionVolumes(int deviceIndex)
{
    std::vector<float> out;

    out.resize(sessions[deviceIndex].size());
    for (int j = 0; j < out.size(); j++)
    {
        HRESULT err = sessions[deviceIndex][j].sessionVolume->GetMasterVolume(&(out[j]));
        if (err != S_OK)
            out[j] = -1;
    }

    return out;
}

float AudioManager::GetSessionVolume(int deviceIndex, int sessionIndex)
{
    if (deviceIndex < 0 || deviceIndex >= devices.size())
        return -1;
    if (sessionIndex < 0 || sessionIndex >= sessions[deviceIndex].size())
        return -1;

    float vol;
    HRESULT err = sessions[deviceIndex][sessionIndex].sessionVolume->GetMasterVolume(&vol);
    if (err != S_OK)
        vol = -1;
    return vol;
}
float AudioManager::GetDeviceVolume(int deviceIndex)
{
    if (deviceIndex < 0 || deviceIndex >= devices.size())
        return -1;

    float vol;
    HRESULT err = devices[deviceIndex].deviceVolume->GetMasterVolumeLevelScalar(&vol);
    if (err != S_OK)
        vol = -1;
    return vol;
}
void AudioManager::SetDirectSessionVolume(int deviceIndex, int sessionIndex, float val)
{
    if (val < 0)
        val = 0;
    if (val > 1.0f)
        val = 1;
    if (deviceIndex < 0 || deviceIndex >= devices.size())
        return;
    if (sessionIndex < 0 || sessionIndex >= sessions[deviceIndex].size())
        return;

    HRESULT err = sessions[deviceIndex][sessionIndex].sessionVolume->SetMasterVolume(val,nullptr);

    sessions[deviceIndex][sessionIndex].volume = val;
}
void AudioManager::SetSessionVolume(int deviceIndex, int sessionIndex, float val)
{
    val /= 100.0f; //1.0f will be max device and max session volume
    if (val < 0)
        val = 0;
    if (val > 1.0f)
        val = 1;
    if (deviceIndex < 0 || deviceIndex >= devices.size())
        return;
    if (sessionIndex < 0 || sessionIndex >= sessions[deviceIndex].size())
        return;

    float initD = GetDeviceVolume(deviceIndex);
    std::vector<float> sessionVolumes = GetSessionVolumes(deviceIndex);

    int highestSession = 0; //highest session after volume change
    float highestVol;

    for (int i = 0; i < sessionVolumes.size(); i++) //converrt to true volumes
        sessionVolumes[i] *= initD;
    sessionVolumes[sessionIndex] = val;
    
    //get highest volume after change
    highestVol = sessionVolumes[0];
    for (int i = 1; i < sessionVolumes.size(); i++)
    {
        if (highestVol < sessionVolumes[i])
        {
            highestSession = i;
            highestVol = sessionVolumes[i];
        }
    }

    float newD = highestVol;
    SetDeviceVolume(deviceIndex, newD);
    if (initD == 0) //device volume is 0, assume other sessions are correctly set to 0
    {
        SetDirectSessionVolume(deviceIndex, sessionIndex, 1.0f);
        return;
    }
    if (newD == 0) //this session is now 0, and all other sessions are 0 too.
    {
        SetDirectSessionVolume(deviceIndex, sessionIndex, 0.0f);
        return;
    }

    for (int i = 0; i < sessions[deviceIndex].size(); i++)
        SetDirectSessionVolume(deviceIndex,i,sessionVolumes[i]/newD);
    
    //have device volume be maximum across all sessions such that loudest session is always at 1.0
    //OR have loudest at 0.999 so volume changes can be detected

    //if errored, remove that session


    //todo
    //function to flag session or device for deletion
    //function to clear deleted sessions and devices
    //work on system to add new sessions
    //true set volume that modifies other volumes DONE
    //finish set endpoint volume DONE
    //when device volume is changed on PC, treat it as global volume change
    //when session volume is changed on PC, treat it as specifially changing that volume
    //GetForegroundWindow for currently focused window
}
void AudioManager::OffsetSessionVolume(int deviceIndex, int sessionIndex, float offset)
{
    if (deviceIndex < 0 || deviceIndex >= devices.size())
        return;
    if (sessionIndex < 0 || sessionIndex >= sessions[deviceIndex].size())
        return;
    offset /= 100.0f; //1.0f will be max device and max session volume

    float dVol;
    HRESULT err = devices[deviceIndex].deviceVolume->GetMasterVolumeLevelScalar(&dVol);
    if (err != S_OK)
        return;

    float sVol = -1;
    err = sessions[deviceIndex][sessionIndex].sessionVolume->GetMasterVolume(&sVol);
    if (err != S_OK)
        return;

    SetSessionVolume(deviceIndex, sessionIndex, 100*((dVol*sVol)+offset));
}
void AudioManager::SetAllSessionVolumes(int deviceIndex, float val)
{
    val /= 100.0f; //1.0f will be max device and max session volume
    if (val < 0)
        val = 0;
    if (val > 1.0f)
        val = 1;
    if (deviceIndex < 0 || deviceIndex >= devices.size())
        return;
    
    SetDeviceVolume(deviceIndex, val);
    for (int i = 0; i < sessions[deviceIndex].size(); i++)
        SetDirectSessionVolume(deviceIndex, i, 1.0f);

    float initD = GetDeviceVolume(deviceIndex);
    std::vector<float> sessionVolumes = GetSessionVolumes(deviceIndex);
}
void AudioManager::SetDeviceVolume(int deviceIndex, float val)
{
    if (val < 0)
        val = 0;
    if (val > 1.0f)
        val = 1;
    if (deviceIndex < 0 || deviceIndex >= devices.size())
        return;

    devices[deviceIndex].deviceVolume->SetMasterVolumeLevelScalar(val, nullptr);

    devices[deviceIndex].volume = val;
}