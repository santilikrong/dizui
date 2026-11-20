#include "Application.hpp"
#include <iostream>
#include <exception>
#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/propertyconfigurator.h>
#include <filesystem>

int main()
{
    try
    {
        Application app;
        
        if (std::filesystem::exists("log4cxx.properties"))
        {
            log4cxx::PropertyConfigurator::configure("log4cxx.properties");
        }
        else
        {
            log4cxx::BasicConfigurator::configure();
            log4cxx::Logger::getRootLogger()->setLevel(log4cxx::Level::getInfo());
        }

        app.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal Exception occurred: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
