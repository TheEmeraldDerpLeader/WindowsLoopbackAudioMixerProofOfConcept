#include <iostream>
#include <string>

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include <AudioManager.hpp>
#include <ProcessCapture.hpp>

/*Todo:
    Keybind system to tie volume control to keybinds
        - complex keybinds, e.g. ctrl shift up arrow, tap ctrl to lower volume tap shift to raise volume
*/


class CLInterface
{
public:
    std::vector<CaptureSource> sources;
    std::vector<CaptureSourceStream> streams;
    AudioManager am;

    int ctrlDev = -1; //for testing session specific control
    int ctrlSes = -1;

    void CommandHelp()
    {
        std::cout << "Commands\
        \nHelp: displays this message\
        \nSources: view all active sources availible for loopback\
        \nStreams: view all currently active loopback streams\
        \nCapture <index>: start a stream for a given source index in the last sources call\
        \nStop <index>: stop a stream for a given index\
        \nExit: stop all streams and exit this application\n";
    }
    void CommandSources()
    {
        GetAllAudioSessionSources(sources);
        //Remove sources with the same process id since loopback can't distinguish between streams on the same process
        for (int i = 0; i < sources.size(); i++)
        {
            for (int j = sources.size()-1; j > i; j--)
            {
                if (sources[i].processID == sources[j].processID)
                    sources.erase(sources.begin()+j);
            }
        }

        //remove this process from sources
        DWORD thisPID = GetCurrentProcessId();
        for (int i = 0; i < sources.size(); i++)
            if (sources[i].processID == thisPID)
            {
                sources.erase(sources.begin()+i);
                break;
            }

        for (int i = 0; i < sources.size(); i++)
            //std::wcout << sources[i].deviceName << "  -  " << sources[i].processID << " : " << sources[i].processName << '\n';
            std::wcout << i << " -- " << sources[i].sessionID << " : " << sources[i].processName << '\n';
    }
    void CommandStreams()
    {
        for (int i = 0; i < streams.size(); i++)
            std::wcout << i << " -- " << streams[i].source.processID << " : " << streams[i].source.processName << '\n';
    }
    void CommandCapture(int i)
    {
        if (i < 0 || i >= sources.size())
        {
            std::cout << "Invalid index\n";
            return;
        }
        //one process can't have multiple streams tied to it for some reason
        //from further testing, in general creating a second audio client on the same process, even if the original audio client was deleted, results in an error (E_UNEXPECTED on IAudioClient::Initialize) and won't allow for events to be made
        for (int j = 0; j < streams.size(); j++)
            if (streams[j].source.processID == sources[i].processID)
            {
                std::cout << "Invalid process: this process already has a stream\n";
                return;
            }
            
        
        streams.push_back(sources[i].GetStream());
    }
    void CommandStop(int i)
    {
        if (i < 0 || i >= streams.size())
        {
            std::cout << "Invalid index\n";
            return;
        }
        streams.erase(streams.begin()+i);
    }
    void CommandExit()
    {
        streams.clear();
    }
    void CommandSetVolume(int device, int session, float v)
    {
        am.SetSessionVolume(device, session, v);
        std::cout << "Volume: " << am.GetSessionVolume(device, session) << '\n';
    }
    void CommandResetSessions()
    {
        am.ResetSessions();
    }
    void CommandFullPrint()
    {
        for (int i = 0; i < am.devices.size(); i++)
        {
            std::wcout << "Device: " << am.devices[i].deviceName << "\n";
            for (int j = 0; j < am.sessions[i].size(); j++)
            {
                std::wcout << '\t' << am.sessions[i][j].name() << '\n';
            }
            std::cout << '\n';
        }
    }

    void CommandTest()
    {
        std::cin >> ctrlDev;
        std::cin >> ctrlSes;
        float vol = 0;
        std::cin >> vol;
        for (int i = 0; i < am.devices.size(); i++)
        {
            std::wcout << "Device: " << am.devices[i].deviceName << "\n";
            for (int j = 0; j < am.sessions[i].size(); j++)
            {
                std::wcout << '\t' << am.sessions[i][j].volume() << '\n';
            }
            std::cout << '\n';
        }

        //am.SetAllSessionVolumes(ctrlDev, vol);
    }

    std::string TryReadCommand()
    {
        std::string command;
        std::cin >> command;

        //convert command to lowercase
        for (int i = 0; i < command.size(); i++)
            if (command[i] >= 'A' && command[i] <= 'Z')
                command[i] += ('a'-'A');

        return command;
    }
    int TryReadInt()
    {
        int num = -1;
        std::cin >> num;
        return num;
    }
    float TryReadFloat()
    {
        float num = -1;
        std::cin >> num;
        return num;
    }
};

int main2()
{

    /*std::vector<int> LOL;
    std::vector<int> LOL1;
    for (int i = 0; i < 100000000; i++)
    {
        LOL.push_back(rand());
        LOL1.push_back(rand());
    }
    for (int i = 0; i < 100000000; i++)
        LOL1[i] = LOL[i]+LOL1[i];*/


    CLInterface cli;

    //initial message
    std::cout << "--- Windows Audio Loopback Proof of Concept ---\n\n";
    cli.CommandHelp();
    std::cout << '\n';
    cli.CommandSources();

    while (true)
    {
        std::cout << '\n';
        std::cout << "Enter a command: ";
        std::string command = cli.TryReadCommand();

        if (command == "help")
            cli.CommandHelp();
        if (command == "sources")
            cli.CommandSources();
        if (command == "streams")
            cli.CommandStreams();
        if (command == "capture")
        {
            int index = cli.TryReadInt();
            cli.CommandCapture(index);
        }
        if (command == "stop")
        {
            int index = cli.TryReadInt();
            cli.CommandStop(index);
        }
        if (command == "exit")
        {
            cli.CommandExit();
            break;
        }
        if (command == "setvolume")
        {
            int device = cli.TryReadInt();
            int session = cli.TryReadInt();
            float vol = cli.TryReadFloat();
            cli.CommandSetVolume(device, session, vol);
        }
        if (command == "resetsessions")
        {
            cli.CommandResetSessions();
        }
        if (command == "fullprint")
        {
            cli.CommandFullPrint();
        }
        if (command == "test")
        {
            cli.CommandTest();
        }

        if (command == "p")
            cli.am.OffsetSessionVolume(cli.ctrlDev, cli.ctrlSes, 2);
        
        if (command == "l")
            cli.am.OffsetSessionVolume(cli.ctrlDev, cli.ctrlSes, -2);


    }

    /*
    sf::RenderWindow window(sf::VideoMode({200, 200}), "SFML works!");
    window.setFramerateLimit(60);
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
    */

	return 0;
}

int main()
{
    CoInitializeEx(0,COINIT::COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    int out = main2();

    MFShutdown();
    CoUninitialize();
    return out;
}
