#include "WindowManager.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>

#include "WindowManager.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>

WindowManager::WindowManager(int width, int height, const std::string &title)
{
    if (!glfwInit())
    {
        throw std::runtime_error("Impossible d'initialiser GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window)
    {
        glfwTerminate();
        throw std::runtime_error("Échec de la création de la fenêtre GLFW");
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // Activation du VSync
}
// Le reste du fichier source WindowManager.cpp demeure inchangé...

WindowManager::~WindowManager()
{
    if (m_window)
    {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

bool WindowManager::shouldClose() const
{
    return glfwWindowShouldClose(m_window);
}

void WindowManager::swapBuffers()
{
    glfwSwapBuffers(m_window);
}

void WindowManager::pollEvents()
{
    glfwPollEvents();
}
