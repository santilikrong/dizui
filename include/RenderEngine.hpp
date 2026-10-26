#pragma once
struct GLFWwindow;

class RenderEngine {
public:
    RenderEngine(GLFWwindow* window);
    ~RenderEngine();

    void startFrame();
    void endFrame(int display_w, int display_h);
};
