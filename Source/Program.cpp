#include <iostream>
#include <string>

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include <Windows.h>
#include <Psapi.h>

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

void WindowsTest()
{
    DWORD processes[2048];
    DWORD retSize;
    EnumProcesses(processes, sizeof(DWORD)*2048, &retSize);
    for (int i = 0; i < retSize/sizeof(DWORD); i++)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processes[i]);
        if (process != NULL)
        {
            wchar_t processName[2048];
            GetModuleBaseName(process, NULL, processName, 2048); //this errors
            //std::wcout << std::wstring(processName) << '\n';
            CloseHandle(process);
        }
    }
}