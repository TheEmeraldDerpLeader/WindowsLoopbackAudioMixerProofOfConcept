#include <iostream>
#include <string>

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include <AudioManager.hpp>

int main2()
{
	std::cout << "Test!\n";
    AudioManager am;

    std::cout << "\n";
    std::vector<CaptureSource> sources;
    GetAllAudioSessionSources(sources);
    for (int i = 0; i < sources.size(); i++)
        std::wcout << sources[i].deviceName << "  -  " << sources[i].processID << " : " << sources[i].processName << '\n';

    std::cout << "\n";

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

int main()
{
    CoInitializeEx(0,COINIT::COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    int out = main2();

    MFShutdown();
    CoUninitialize();
    return out;
}
