#include <iostream>
#include <string>

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include <AudioManager.hpp>
#include <ProcessCapture.hpp>


class CLInterface
{
public:
    std::vector<CaptureSource> sources;
    std::vector<CaptureSourceStream> streams;
    AudioManager am;

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
            std::wcout << i << " -- " << sources[i].processID << " : " << sources[i].processName << '\n';
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
    int TryReadNum()
    {
        int num = -1;
        std::cin >> num;
        return num;
    }
};

int main2()
{
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
            int index = cli.TryReadNum();
            cli.CommandCapture(index);
        }
        if (command == "stop")
        {
            int index = cli.TryReadNum();
            cli.CommandStop(index);
        }
        if (command == "exit")
        {
            cli.CommandExit();
            break;
        }
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
