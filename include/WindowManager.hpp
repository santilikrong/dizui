#pragma once
#include <string>

struct GLFWwindow;

class WindowManager {
public:
    WindowManager(int width, int height, const std::string& title);
    ~WindowManager();

    bool shouldClose() const;
    void swapBuffers();
    void pollEvents();
    GLFWwindow* getNativeWindow() const { return m_window; }

private:
    GLFWwindow* m_window = nullptr;
};
