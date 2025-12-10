#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <algorithm>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

// -------------------------- small helpers -------------------------- //

struct Vec3 {
    float x, y, z;
};

static Vec3 make_vec3(float x, float y, float z) {
    return {x, y, z};
}

static Vec3 sub(const Vec3 &a, const Vec3 &b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static Vec3 normalize_vec3(const Vec3 &v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.0f) return {0.0f, 0.0f, 0.0f};
    return {v.x / len, v.y / len, v.z / len};
}

static std::string readFile(const std::string &path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

static GLuint compileShader(GLenum type, const std::string &src) {
    GLuint shader = glCreateShader(type);
    const char *c_str = src.c_str();
    glShaderSource(shader, 1, &c_str, nullptr);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(logLen, '\0');
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        std::string typeStr = (type == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT";
        std::cerr << "Error compiling " << typeStr << " shader:\n"
                  << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint createProgram(const std::string &vsPath, const std::string &fsPath) {
    std::string vsSource = readFile(vsPath);
    std::string fsSource = readFile(fsPath);

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource);
    if (!vs) return 0;
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint status = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(logLen, '\0');
        glGetProgramInfoLog(prog, logLen, nullptr, log.data());
        std::cerr << "Error linking shader program:\n"
                  << log << std::endl;
        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

static void errorCallback(int code, const char *desc) {
    std::cerr << "GLFW error (" << code << "): " << desc << std::endl;
}

// ------------------------ render settings -------------------------- //

struct RenderSettings {
    // Camera
    float camDistance = 4.0f;
    float camYaw      = 0.0f;   // around Y
    float camPitch    = 0.4f;   // up/down
    float fov         = 1.0f;
    bool  autoRotate  = true;
    float rotationSpeed = 0.2f; // radians per second

    // Fractal
    float power       = 8.0f;
    int   maxIterations = 18;
    float bailout     = 2.0f;

    // Raymarch
    int   maxSteps    = 200;
    float maxDist     = 25.0f;
    float epsilon     = 0.001f;

    // Shading
    bool  enableAO      = true;
    bool  enableShadows = true;
    float colorA[3]   = {0.2f, 0.3f, 0.6f};
    float colorB[3]   = {0.8f, 0.9f, 1.0f};
};

int main() {
    // ----------------- GLFW + OpenGL init ----------------- //
    glfwSetErrorCallback(errorCallback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(1280, 720, "GPU Mandelbulb (ImGui)", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    glewExperimental = GL_TRUE;
    GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW: "
                  << reinterpret_cast<const char *>(glewGetErrorString(glewStatus))
                  << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Clear GLEW spurious error
    glGetError();

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << std::endl;

    // ------------------- Shader program ------------------- //
    GLuint program = 0;
    try {
        program = createProgram("../shaders/mandelbulb.vert",
                                "../shaders/mandelbulb.frag");
    } catch (const std::exception &e) {
        std::cerr << "Exception while creating program: " << e.what() << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!program) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // ------------------- Fullscreen quad ------------------ //
    float quadVertices[] = {
        // positions   // texcoords
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // -------------- Uniform locations (cached) ------------- //
    glUseProgram(program);
    GLint uTimeLoc          = glGetUniformLocation(program, "u_time");
    GLint uResLoc           = glGetUniformLocation(program, "u_resolution");
    GLint uCamPosLoc        = glGetUniformLocation(program, "u_camPos");
    GLint uCamForwardLoc    = glGetUniformLocation(program, "u_camForward");
    GLint uCamRightLoc      = glGetUniformLocation(program, "u_camRight");
    GLint uCamUpLoc         = glGetUniformLocation(program, "u_camUp");
    GLint uFovLoc           = glGetUniformLocation(program, "u_fov");
    GLint uPowerLoc         = glGetUniformLocation(program, "u_power");
    GLint uMaxIterLoc       = glGetUniformLocation(program, "u_maxIter");
    GLint uBailoutLoc       = glGetUniformLocation(program, "u_bailout");
    GLint uMaxStepsLoc      = glGetUniformLocation(program, "u_maxSteps");
    GLint uMaxDistLoc       = glGetUniformLocation(program, "u_maxDist");
    GLint uEpsilonLoc       = glGetUniformLocation(program, "u_epsilon");
    GLint uColorALoc        = glGetUniformLocation(program, "u_colorA");
    GLint uColorBLoc        = glGetUniformLocation(program, "u_colorB");
    GLint uEnableAOLoc      = glGetUniformLocation(program, "u_enableAO");
    GLint uEnableShadowsLoc = glGetUniformLocation(program, "u_enableShadows");
    glUseProgram(0);

    // -------------- ImGui initialization ------------------- //
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    const char *glsl_version = "#version 330";
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Enable SRGB framebuffer if available
    if (GLEW_EXT_framebuffer_sRGB || GLEW_VERSION_3_0) {
        glEnable(GL_FRAMEBUFFER_SRGB);
    }

    RenderSettings settings;

    // ------------------------ Main loop -------------------- //
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        float time = static_cast<float>(glfwGetTime());
        if (settings.autoRotate) {
            settings.camYaw = time * settings.rotationSpeed;
        }

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ---------- ImGui UI: Mandelbulb controls ----------- //
        ImGui::Begin("Mandelbulb Controls");

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Auto rotate", &settings.autoRotate);
            ImGui::SliderFloat("Distance", &settings.camDistance, 2.0f, 12.0f);
            ImGui::SliderFloat("Yaw", &settings.camYaw, -3.1415f, 3.1415f);
            ImGui::SliderFloat("Pitch", &settings.camPitch, -1.5f, 1.5f);
            ImGui::SliderFloat("FOV", &settings.fov, 0.3f, 2.0f);
            ImGui::SliderFloat("Rotation speed", &settings.rotationSpeed, 0.0f, 1.0f);
            if (ImGui::Button("Reset camera")) {
                settings.camDistance = 4.0f;
                settings.camYaw      = 0.0f;
                settings.camPitch    = 0.4f;
                settings.fov         = 1.0f;
            }
        }

        if (ImGui::CollapsingHeader("Fractal", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Power", &settings.power, 2.0f, 16.0f);
            ImGui::SliderInt("Iterations", &settings.maxIterations, 4, 64);
            ImGui::SliderFloat("Bailout radius", &settings.bailout, 1.0f, 6.0f);
        }

        if (ImGui::CollapsingHeader("Raymarch")) {
            ImGui::SliderInt("Max steps", &settings.maxSteps, 50, 512);
            ImGui::SliderFloat("Max distance", &settings.maxDist, 4.0f, 60.0f);
            ImGui::SliderFloat("Epsilon", &settings.epsilon,
                               0.0001f, 0.01f, "%.5f",
                               ImGuiSliderFlags_Logarithmic);
        }

        if (ImGui::CollapsingHeader("Shading / Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Ambient occlusion", &settings.enableAO);
            ImGui::Checkbox("Soft shadows", &settings.enableShadows);
            ImGui::ColorEdit3("Color A", settings.colorA);
            ImGui::ColorEdit3("Color B", settings.colorB);
        }

        if (ImGui::Button("Reset everything")) {
            settings = RenderSettings(); // resets to defaults
        }

        ImGui::Text("Tip: tweak power, iterations and colors to\n"
                    "generate very different Mandelbulb looks.");

        ImGui::End();

        // -------------- Compute camera basis ---------------- //
        settings.camPitch = std::clamp(settings.camPitch, -1.5f, 1.5f);
        settings.camDistance = std::max(settings.camDistance, 0.5f);

        float cp = std::cos(settings.camPitch);
        float sp = std::sin(settings.camPitch);
        float cy = std::cos(settings.camYaw);
        float sy = std::sin(settings.camYaw);

        Vec3 camPos = {
            settings.camDistance * cp * cy,
            settings.camDistance * sp,
            settings.camDistance * cp * sy
        };
        Vec3 target  = {0.0f, 0.0f, 0.0f};

        Vec3 forward = normalize_vec3(sub(target, camPos));
        Vec3 worldUp = {0.0f, 1.0f, 0.0f};
        Vec3 right   = normalize_vec3(cross(forward, worldUp));
        Vec3 up      = cross(right, forward);

        // -------------- Rendering: fractal ------------------ //
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glUniform1f(uTimeLoc, time);
        glUniform2f(uResLoc, static_cast<float>(width), static_cast<float>(height));

        glUniform3f(uCamPosLoc, camPos.x, camPos.y, camPos.z);
        glUniform3f(uCamForwardLoc, forward.x, forward.y, forward.z);
        glUniform3f(uCamRightLoc, right.x, right.y, right.z);
        glUniform3f(uCamUpLoc, up.x, up.y, up.z);
        glUniform1f(uFovLoc, settings.fov);

        glUniform1f(uPowerLoc, settings.power);
        glUniform1i(uMaxIterLoc, settings.maxIterations);
        glUniform1f(uBailoutLoc, settings.bailout);

        glUniform1i(uMaxStepsLoc, settings.maxSteps);
        glUniform1f(uMaxDistLoc, settings.maxDist);
        glUniform1f(uEpsilonLoc, settings.epsilon);

        glUniform3f(uColorALoc, settings.colorA[0], settings.colorA[1], settings.colorA[2]);
        glUniform3f(uColorBLoc, settings.colorB[0], settings.colorB[1], settings.colorB[2]);
        glUniform1i(uEnableAOLoc, settings.enableAO ? 1 : 0);
        glUniform1i(uEnableShadowsLoc, settings.enableShadows ? 1 : 0);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glUseProgram(0);

        // -------------- ImGui render pass ------------------- //
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ---------------------- Cleanup ----------------------- //
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
