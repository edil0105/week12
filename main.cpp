// =====================================================================
//  КОМПЬЮТЕРЛІК ГРАФИКА — бір файлдық жоба
//
//  1-АПТА:    терезе, фон анимациясы, FPS
//  СЕМИНАР 4: uniform, dt, цикл "жаңарту (update)" және "сызу (draw)"
//  5-АПТА:    индекс буфері (EBO), glDrawElements
//  7-АПТА:    координаттар кеңістіктері, MVP
//  8-АПТА:    тереңдік буфері, back-face culling, бес куб
//  9-АПТА:    камера моделі, LookAt, пернетақтамен жылжу
//  10-АПТА:   толық еркін камера — тінтуір (yaw/pitch), дөңгелек (fov)
//  11-АПТА:   текстуралар — stb_image, UV, орау, сүзгі, mipmap
//  12-АПТА:   БІРНЕШЕ ТЕКСТУРА — текстура бірліктері (texture units),
//             екі sampler2D, mix() коэффициенті uniform арқылы
//             - базалық:     tex1 = container, tex2 = pattern, blend (↑/↓ × dt)
//             - 1-тапсырма:  U — glActiveTexture жолдары "комментарийде"
//             - 2-тапсырма:  X — glUniform1i-ге бірлік нөмірінің орнына ID
//             - 3-тапсырма:  1/2/3/4 — mix / a*b / a+b / clamp(a+b)
//             - 4-тапсырма:  5 — logo.png альфамен жапсыру (decal)
//             - 5-тапсырма:  6 — mix(a, b, vTexCoord.y), 7 — smoothstep
//             - қосымша:     Қ1 8 — үш текстура, Қ2 B — автоматты blend,
//                            Қ3 F — әр жаққа бөлек blend, Қ4 O — логотипті айналдыру
//
//  Басқару:
//      Тінтуір / дөңгелек   — қарау / fov
//      W A S D, Space, Ctrl — жүру, Shift — жүгіру
//      ↑ / ↓                — blend үлкейту / кішірейту
//      1 … 8                — араластыру тәсілі (жоғарыдағы тізім)
//      U  X                 — 1-тапсырма / 2-тапсырма (әдейі қате)
//      B  F  O              — Қ2 / Қ3 / Қ4
//      P                    — күйді консольге шығару
//      R                    — камера мен blend-ті бастапқы күйге қайтару
//      I  L  M              — invert Y / pitch шектеуі / тінтуір режимі
//      F12  ESC             — скриншот / шығу
//
//  Іске қосу:  ./build/bin/cg                — қалыпты режим
//              ./build/bin/cg --screenshots  — README-дегі скриншоттар
// =====================================================================

#include <glad/gl.h>      // МІНДЕТТІ: glad әрқашан GLFW-дан БҰРЫН
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifndef ASSET_DIR
#define ASSET_DIR "assets"
#endif
#ifndef SCREENSHOT_DIR
#define SCREENSHOT_DIR "screenshots"
#endif

int gWidth = 800, gHeight = 600;

// =====================================================================
//  10-АПТА: КАМЕРА
// =====================================================================
struct Camera {
    glm::vec3 pos;
    glm::vec3 front;
    glm::vec3 up;
};

const Camera START_CAMERA = {
    glm::vec3(0.0f, 0.0f,  3.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
    glm::vec3(0.0f, 1.0f,  0.0f)
};

Camera freeCam = START_CAMERA;
glm::vec3& camPos   = freeCam.pos;
glm::vec3& camFront = freeCam.front;
glm::vec3& camUp    = freeCam.up;

const float START_YAW   = -90.0f;
const float START_PITCH =   0.0f;
const float START_FOV   =  45.0f;

float yaw   = START_YAW;
float pitch = START_PITCH;
float lastX = 400.0f;
float lastY = 300.0f;
bool  firstMouse = true;
float fov   = START_FOV;

const float FOV_MIN =  1.0f;
const float FOV_MAX = 90.0f;

bool pitchLimit = true;
bool invertY    = false;
// WSL-де (WSLg) курсорды ұстау дұрыс жұмыс істемейді — сүйреу режимінен бастаймыз
bool dragMode   = std::getenv("WSL_DISTRO_NAME") != nullptr;

float angle = 0.0f;   // кубтардың айналу бұрышы

// =====================================================================
//  12-АПТА: АРАЛАСТЫРУ КҮЙІ
// =====================================================================

// Фрагмент шейдеріндегі `mode` uniform-ының мәндері (1…8 пернелері)
enum BlendMode {
    MODE_MIX = 0,      // 1: mix(a, b, blend)              — базалық, 3-тапсырма
    MODE_MULTIPLY,     // 2: a * b                          — 3-тапсырма
    MODE_ADD,          // 3: a + b                          — 3-тапсырма
    MODE_ADD_CLAMP,    // 4: clamp(a + b, 0, 1)             — 3-тапсырма
    MODE_DECAL,        // 5: mix(base, decal, decal.a)      — 4-тапсырма
    MODE_GRADIENT,     // 6: mix(a, b, vTexCoord.y)         — 5-тапсырма
    MODE_SMOOTH,       // 7: mix(a, b, smoothstep(...))     — 5-тапсырма
    MODE_THREE,        // 8: үш текстура                    — Қ1
    MODE_COUNT
};

const char* MODE_NAMES[MODE_COUNT] = {
    "mix(a, b, blend)",
    "a * b",
    "a + b",
    "clamp(a + b, 0, 1)",
    "mix(base, decal, decal.a)",
    "mix(a, b, vTexCoord.y)",
    "mix(a, b, smoothstep(0.3, 0.7, y))",
    "үш текстура: mix(mix(a, b, blend), decal, decal.a)",
};

struct BlendState {
    int   mode        = MODE_MIX;
    float blend       = 0.5f;    // 5-қадам: жаһандық коэффициент
    bool  brokenUnits = false;   // 1-тапсырма: glActiveTexture жоқ
    bool  wrongId     = false;   // 2-тапсырма: texture2 = tex2 (ID), 1 емес
    bool  autoBlend   = false;   // Қ2
    bool  perFace     = false;   // Қ3
    bool  rotateLogo  = false;   // Қ4
};

BlendState st;
float logoAngle = 0.0f;          // Қ4: айналу бұрышы (O қосулы кезде ғана өседі)

// 1-тапсырма қосылған сәтте 1 және 2-бірліктерді тазалаймыз. Әйтпесе оларда
// алдыңғы кадрлардан қалған текстура тұрып қалады да, бағдарламаны
// glActiveTexture-сыз ІСКЕ ҚОСҚАНДАҒЫ күй көрінбейді.
bool clearUnitsRequest = false;

// =====================================================================
//  Callback-тер (10-апта)
// =====================================================================
void updateCameraFront() {
    glm::vec3 dir;
    dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    dir.y = sin(glm::radians(pitch));
    dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    camFront = glm::normalize(dir);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (dragMode && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
        return;
    }
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;
    if (invertY) yoffset = -yoffset;
    lastX = (float)xpos;
    lastY = (float)ypos;

    const float sensitivity = 0.1f;
    yaw   += xoffset * sensitivity;
    pitch += yoffset * sensitivity;

    if (yaw >  360.0f) yaw -= 360.0f;
    if (yaw < -360.0f) yaw += 360.0f;

    if (pitchLimit) {
        if (pitch >  89.0f) pitch =  89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }
    updateCameraFront();
}

void scroll_callback(GLFWwindow*, double, double yoffset) {
    fov -= (float)yoffset;
    if (fov < FOV_MIN) fov = FOV_MIN;
    if (fov > FOV_MAX) fov = FOV_MAX;
}

void framebuffer_size_callback(GLFWwindow*, int width, int height) {
    if (width == 0 || height == 0) return;
    gWidth  = width;
    gHeight = height;
    glViewport(0, 0, width, height);
}

void saveScreenshot(const std::string& path) {
    std::vector<unsigned char> pixels((size_t)gWidth * gHeight * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, gWidth, gHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    if (stbi_write_png(path.c_str(), gWidth, gHeight, 3, pixels.data(), gWidth * 3))
        std::cout << "Скриншот: " << path << "\n";
    else
        std::cerr << "Скриншот сақталмады: " << path << "\n";
}

// Бір рет басу: перне басылған СӘТТЕ ғана true
bool keyPressedOnce(GLFWwindow* window, int key) {
    static bool last[GLFW_KEY_LAST + 1] = {};
    bool now   = glfwGetKey(window, key) == GLFW_PRESS;
    bool fired = now && !last[key];
    last[key]  = now;
    return fired;
}

void applyMouseMode(GLFWwindow* window) {
    glfwSetInputMode(window, GLFW_CURSOR, dragMode ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    firstMouse = true;
}

void resetCamera() {
    freeCam = START_CAMERA;
    yaw   = START_YAW;
    pitch = START_PITCH;
    fov   = START_FOV;
    updateCameraFront();
}

void printState() {
    std::printf("camPos = (%.2f, %.2f, %.2f)  yaw = %.1f  pitch = %.1f  fov = %.1f\n",
                camPos.x, camPos.y, camPos.z, yaw, pitch, fov);
    std::printf("тәсіл = %s  blend = %.2f\n", MODE_NAMES[st.mode], st.blend);
    std::printf("glActiveTexture жоқ: %s  ID қатесі: %s  авто: %s  әр жақ: %s  айналу: %s\n",
                st.brokenUnits ? "иә" : "жоқ", st.wrongId ? "иә" : "жоқ",
                st.autoBlend ? "иә" : "жоқ", st.perFace ? "иә" : "жоқ",
                st.rotateLogo ? "иә" : "жоқ");
}

void toggle(bool& flag, const char* name) {
    flag = !flag;
    std::cout << name << ": " << (flag ? "ҚОСУЛЫ" : "ӨШІРУЛІ") << "\n";
}

// =====================================================================
//  Пернетақта — әр кадрда
// =====================================================================
void processInput(GLFWwindow* window, float dt) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // --- Жылжу (9–10-апта) ---
    float camSpeed = 2.5f * dt;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camSpeed *= 2.0f;

    glm::vec3 right = glm::normalize(glm::cross(camFront, camUp));
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camPos += camSpeed * camFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camPos -= camSpeed * camFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camPos -= camSpeed * right;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camPos += camSpeed * right;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        camPos += camSpeed * glm::vec3(0.0f, 1.0f, 0.0f);
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        camPos -= camSpeed * glm::vec3(0.0f, 1.0f, 0.0f);

    // --- 12-апта, 5-қадам: blend. Үздіксіз әрекет — dt-ге көбейтеміз,
    //     әйтпесе бір кадрда 0-ден 1-ге секіріп кетеді ---
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        st.blend += 0.5f * dt;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        st.blend -= 0.5f * dt;
    if (st.blend > 1.0f) st.blend = 1.0f;
    if (st.blend < 0.0f) st.blend = 0.0f;

    // --- 1…8: араластыру тәсілі ---
    for (int i = 0; i < MODE_COUNT; i++) {
        if (keyPressedOnce(window, GLFW_KEY_1 + i)) {
            st.mode = i;
            std::cout << "тәсіл " << (i + 1) << ": " << MODE_NAMES[i] << "\n";
        }
    }

    // --- 1, 2-тапсырмалар (әдейі қате) ---
    if (keyPressedOnce(window, GLFW_KEY_U)) {
        toggle(st.brokenUnits, "glActiveTexture жолдары өшірілді (1-тапсырма)");
        if (st.brokenUnits) clearUnitsRequest = true;
    }
    if (keyPressedOnce(window, GLFW_KEY_X))
        toggle(st.wrongId, "texture2 = tex2 ID (2-тапсырма)");

    // --- Қосымша ---
    if (keyPressedOnce(window, GLFW_KEY_B)) toggle(st.autoBlend,  "Автоматты blend (Қ2)");
    if (keyPressedOnce(window, GLFW_KEY_F)) toggle(st.perFace,    "Әр жаққа бөлек blend (Қ3)");
    if (keyPressedOnce(window, GLFW_KEY_O)) toggle(st.rotateLogo, "Логотипті айналдыру (Қ4)");

    // --- 10-аптадан қалған пернелер ---
    if (keyPressedOnce(window, GLFW_KEY_I)) {
        invertY = !invertY;
        std::cout << "invertY = " << (invertY ? "true" : "false") << "\n";
    }
    if (keyPressedOnce(window, GLFW_KEY_P)) printState();
    if (keyPressedOnce(window, GLFW_KEY_R)) {
        resetCamera();
        st = BlendState();
        logoAngle = 0.0f;
        std::cout << "Камера мен blend бастапқы күйде\n";
    }
    if (keyPressedOnce(window, GLFW_KEY_L)) {
        pitchLimit = !pitchLimit;
        if (pitchLimit) {
            if (pitch >  89.0f) pitch =  89.0f;
            if (pitch < -89.0f) pitch = -89.0f;
            updateCameraFront();
        }
        std::cout << "pitch шектеуі: " << (pitchLimit ? "ҚОСУЛЫ" : "ӨШІРУЛІ") << "\n";
    }
    if (keyPressedOnce(window, GLFW_KEY_M)) {
        dragMode = !dragMode;
        applyMouseMode(window);
        std::cout << "Тінтуір режимі: " << (dragMode ? "сүйреу (ЛКМ басулы)" : "FPS") << "\n";
    }
    if (keyPressedOnce(window, GLFW_KEY_F12)) {
        static int shot = 1;
        saveScreenshot(std::string(SCREENSHOT_DIR) + "/shot-" + std::to_string(shot++) + ".png");
    }
}

// =====================================================================
//  ШЕЙДЕРЛЕР
// =====================================================================

// Вершина шейдері 11-аптадан МҮЛДЕ өзгермеген: екі текстура бір UV жиынын қолданады
const char* texVertexSrc = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    vTexCoord = aTexCoord;
}
)glsl";

// 12-апта, 2-қадам: екі (Қ1-де үш) sampler, араластыру коэффициенті — uniform
const char* texFragmentSrc = R"glsl(
#version 330 core
in  vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D texture1;   // 0-бірлік: container.png
uniform sampler2D texture2;   // 1-бірлік: pattern.png (5-тәсілде logo.png)
uniform sampler2D texture3;   // 2-бірлік: logo.png (тек Қ1)
uniform float blend;
uniform int   mode;
uniform float logoAngle;      // Қ4

// Қ4: логотиптің UV-ін ортасынан бұру. Бұрылғанда UV 0..1-ден шығады —
// logo.png GL_CLAMP_TO_BORDER + мөлдір шекарамен жүктелген, сондықтан
// шеттері қайталанбайды.
vec2 decalUV() {
    vec2  c  = vTexCoord - 0.5;
    float s  = sin(logoAngle);
    float co = cos(logoAngle);
    return vec2(c.x * co - c.y * s, c.x * s + c.y * co) + 0.5;
}

void main() {
    vec4 a = texture(texture1, vTexCoord);
    vec4 b = texture(texture2, vTexCoord);

    if (mode == 0) {                       // 1. сызықтық
        FragColor = mix(a, b, blend);
    } else if (mode == 1) {                // 2. көбейту
        FragColor = a * b;
    } else if (mode == 2) {                // 3. қосу
        FragColor = a + b;
    } else if (mode == 3) {                // 4. қосу, шектеумен
        FragColor = clamp(a + b, 0.0, 1.0);
    } else if (mode == 4) {                // 4-тапсырма: логотип (texture2 = logo.png)
        vec4 base  = a;
        vec4 decal = texture(texture2, decalUV());
        FragColor = mix(base, decal, decal.a);
    } else if (mode == 5) {                // 5-тапсырма: төменнен жоғары
        FragColor = mix(a, b, vTexCoord.y);
    } else if (mode == 6) {                // 5-тапсырма: тіктеу ауысу
        float t = smoothstep(0.3, 0.7, vTexCoord.y);
        FragColor = mix(a, b, t);
    } else {                               // Қ1: үш текстура
        vec4 surface = mix(a, b, blend);
        vec4 decal   = texture(texture3, decalUV());
        FragColor = mix(surface, decal, decal.a);
    }
}
)glsl";

// Еден-торы мен прицел үшін түсті шейдер (оларда UV жоқ)
const char* colorVertexSrc = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 vColor;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    vColor = aColor;
}
)glsl";

const char* colorFragmentSrc = R"glsl(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)glsl";

GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader compile error:\n" << log << "\n";
    }
    return shader;
}

GLuint createShaderProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// =====================================================================
//  11-аптадағы loadTexture — СОЛ ҚАЛПЫНДА (жаңа функция жазылмайды)
// =====================================================================
unsigned int loadTexture(const char* path) {
    unsigned int id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_set_flip_vertically_on_load(true);

    int w, h, n;
    unsigned char* data = stbi_load(path, &w, &h, &n, 0);
    if (data) {
        // logo.png төрт арналы — GL_RGBA. GL_RGB деп жүктесе фоны қара болады.
        GLenum fmt = (n == 4) ? GL_RGBA : GL_RGB;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        std::cout << path << "  " << w << "x" << h << "  арна: " << n << std::endl;
    } else {
        std::cerr << "Сурет жүктелмеді: " << path << std::endl;
    }
    stbi_image_free(data);
    return id;
}

// =====================================================================
//  ГЕОМЕТРИЯ (11-аптадан)
//  Әр жақ — қатар тұрған 6 вершина: алдыңғы, артқы, сол, оң, асты, үсті.
//  Сондықтан Қ3-те кубты glDrawArrays(GL_TRIANGLES, f * 6, 6) деп алты рет
//  сызуға болады.
// =====================================================================
const float cubeVertices[] = {
    //  поз                   UV
    // алдыңғы жақ (z = +0.5)
    -0.5f, -0.5f,  0.5f,   0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,   1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,   1.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,   0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,   1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,   0.0f, 1.0f,
    // артқы жақ (z = -0.5)
     0.5f, -0.5f, -0.5f,   0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,   1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,   0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,   0.0f, 1.0f,
    // сол жақ (x = -0.5)
    -0.5f, -0.5f, -0.5f,   0.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,   1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,   1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,   0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,   1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,   0.0f, 1.0f,
    // оң жақ (x = +0.5)
     0.5f, -0.5f,  0.5f,   0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,   1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,   0.0f, 0.0f,
     0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,   0.0f, 1.0f,
    // асты (y = -0.5)
    -0.5f, -0.5f, -0.5f,   0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,   1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,   1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,   0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,   1.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,   0.0f, 1.0f,
    // үсті (y = +0.5)
    -0.5f,  0.5f,  0.5f,   0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,   1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,   0.0f, 0.0f,
     0.5f,  0.5f, -0.5f,   1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,   0.0f, 1.0f,
};

const float FLOOR_Y   = -1.5f;
const int   GRID_HALF = 20;
const int   RING_N    = 12;
const float RING_R    = 10.0f;

std::vector<float> makeGrid() {
    std::vector<float> v;
    auto line = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        v.insert(v.end(), {a.x, a.y, a.z, c.r, c.g, c.b, b.x, b.y, b.z, c.r, c.g, c.b});
    };
    float h = (float)GRID_HALF;
    for (int i = -GRID_HALF; i <= GRID_HALF; i++) {
        float t = (float)i;
        glm::vec3 c = (i % 5 == 0) ? glm::vec3(0.45f) : glm::vec3(0.25f);
        line({t, FLOOR_Y, -h}, {t, FLOOR_Y, h}, i == 0 ? glm::vec3(0.2f, 0.4f, 1.0f) : c);
        line({-h, FLOOR_Y, t}, {h, FLOOR_Y, t}, i == 0 ? glm::vec3(1.0f, 0.3f, 0.3f) : c);
    }
    return v;
}

const float crosshair[] = {
    -0.03f,  0.0f,  0.0f,   1.0f, 1.0f, 1.0f,
     0.03f,  0.0f,  0.0f,   1.0f, 1.0f, 1.0f,
     0.0f,  -0.03f, 0.0f,   1.0f, 1.0f, 1.0f,
     0.0f,   0.03f, 0.0f,   1.0f, 1.0f, 1.0f,
};

GLuint makeColorVao(const float* data, size_t bytes, GLuint& vbo) {
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, bytes, data, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return vao;
}

// Сахна: 8-аптадағы бес куб
const glm::vec3 cubePositions[] = {
    glm::vec3( 0.0f,  0.0f,  0.0f),
    glm::vec3( 2.0f,  0.5f, -2.0f),
    glm::vec3(-1.8f, -0.3f, -1.0f),
    glm::vec3( 1.2f, -0.8f, -3.5f),
    glm::vec3(-2.2f,  1.0f, -4.0f),
};
const int CUBE_COUNT = sizeof(cubePositions) / sizeof(cubePositions[0]);

// =====================================================================
//  --screenshots: README-дегі скриншоттар
// =====================================================================
struct ShotStep {
    const char* file;
    glm::vec3   pos;
    float       yaw, pitch, fov, angle;
    void      (*setup)(BlendState&);
};

const glm::vec3 SHOT_POS(0.0f, 0.9f, 2.6f);   // кубтың үш жағы көрінетін нүкте
const glm::vec3 FACE_POS(0.0f, 0.0f, 1.9f);   // алдыңғы жаққа тура қарау

const ShotStep SHOTS[] = {
    {"01-blend-0.png",           SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.blend = 0.0f; }},
    {"02-blend-05.png",          SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.blend = 0.5f; }},
    {"03-blend-1.png",           SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.blend = 1.0f; }},
    {"04-broken-units.png",      SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.brokenUnits = true; s.blend = 0.0f; }},
    {"04b-broken-units-1.png",   SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.brokenUnits = true; s.blend = 1.0f; }},
    {"05-wrong-id.png",          SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.wrongId = true; s.blend = 0.5f; }},
    {"05b-wrong-id-three.png",   SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.wrongId = true; s.blend = 1.0f; s.mode = MODE_THREE; }},
    {"06-multiply.png",          SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.mode = MODE_MULTIPLY; }},
    {"07-add.png",               SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.mode = MODE_ADD; }},
    {"07b-add-clamp.png",        SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.mode = MODE_ADD_CLAMP; }},
    {"08-decal.png",             FACE_POS, -90,   0, 45, 0.0f, [](BlendState& s) { s.mode = MODE_DECAL; }},
    {"09-gradient.png",          FACE_POS, -90,   0, 45, 0.0f, [](BlendState& s) { s.mode = MODE_GRADIENT; }},
    {"10-smoothstep.png",        FACE_POS, -90,   0, 45, 0.0f, [](BlendState& s) { s.mode = MODE_SMOOTH; }},
    {"11-three-textures.png",    SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.mode = MODE_THREE; s.blend = 0.5f; }},
    {"12-per-face.png",          SHOT_POS, -90, -16, 45, 0.6f, [](BlendState& s) { s.perFace = true; }},
    {"13-rotate-logo.png",       FACE_POS, -90,   0, 45, 0.0f, [](BlendState& s) { s.mode = MODE_DECAL; s.rotateLogo = true; }},
};
const int SHOT_COUNT = sizeof(SHOTS) / sizeof(SHOTS[0]);

// =====================================================================
//  MAIN
// =====================================================================
int main(int argc, char** argv) {
    bool screenshotMode = (argc > 1 && std::strcmp(argv[1], "--screenshots") == 0);

    if (!glfwInit()) {
        std::cerr << "GLFW іске қосылмады\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(gWidth, gHeight,
                                          "Компьютерлік графика — 12-апта",
                                          nullptr, nullptr);
    if (!window) {
        std::cerr << "Терезе жасалмады. Видеокарта OpenGL 3.3-ті қолдамауы мүмкін.\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSwapInterval(1);

    if (gladLoadGL(glfwGetProcAddress) == 0) {
        std::cerr << "GLAD жүктелмеді\n";
        glfwTerminate();
        return -1;
    }

    applyMouseMode(window);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    glfwGetFramebufferSize(window, &gWidth, &gHeight);
    glViewport(0, 0, gWidth, gHeight);

    std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\n";
    std::cout << "↑/↓ — blend, 1-8 — тәсіл, U — glActiveTexture жоқ, X — ID қатесі,\n"
                 "B — авто blend, F — әр жақ, O — логотипті айналдыру,\n"
                 "P — күй, R — бастапқы күй, M — тінтуір режимі, F12 — скриншот\n";
    if (dragMode)
        std::cout << "WSL: сүйреу режимі — камераны бұру үшін ЛКМ басып тұрып тартыңыз (M — ауыстыру)\n";

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    GLuint shader      = createShaderProgram(texVertexSrc, texFragmentSrc);
    GLuint colorShader = createShaderProgram(colorVertexSrc, colorFragmentSrc);

    GLint tModel = glGetUniformLocation(shader, "model");
    GLint tView  = glGetUniformLocation(shader, "view");
    GLint tProj  = glGetUniformLocation(shader, "projection");
    GLint tBlend = glGetUniformLocation(shader, "blend");
    GLint tMode  = glGetUniformLocation(shader, "mode");
    GLint tAngle = glGetUniformLocation(shader, "logoAngle");
    GLint tTex2  = glGetUniformLocation(shader, "texture2");
    GLint cModel = glGetUniformLocation(colorShader, "model");
    GLint cView  = glGetUniformLocation(colorShader, "view");
    GLint cProj  = glGetUniformLocation(colorShader, "projection");

    // --- Куб: поз + UV, stride 5 (11-апта) ---
    GLuint cubeVao, cubeVbo;
    glGenVertexArrays(1, &cubeVao);
    glGenBuffers(1, &cubeVbo);
    glBindVertexArray(cubeVao);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    GLuint gridVbo, crossVbo;
    std::vector<float> grid = makeGrid();
    int    gridVerts = (int)(grid.size() / 6);
    GLuint gridVao   = makeColorVao(grid.data(), grid.size() * sizeof(float), gridVbo);
    GLuint crossVao  = makeColorVao(crosshair, sizeof(crosshair), crossVbo);

    // --- 12-апта, 1-қадам: екінші (және Қ1 үшін үшінші) текстура ---
    std::string p1 = std::string(ASSET_DIR) + "/container.png";
    std::string p2 = std::string(ASSET_DIR) + "/pattern.png";
    std::string p3 = std::string(ASSET_DIR) + "/logo.png";
    unsigned int tex1 = loadTexture(p1.c_str());
    unsigned int tex2 = loadTexture(p2.c_str());
    unsigned int tex3 = loadTexture(p3.c_str());

    // Қ4: логотип бұрылғанда шеттері қайталанбасын — мөлдір шекара.
    // (Бұрылмаса UV 0..1 ішінде, бұл параметр ештеңені өзгертпейді.)
    glBindTexture(GL_TEXTURE_2D, tex3);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float transparent[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, transparent);

    // 2-тапсырма: текстуралардың нақты ID-лері
    std::cout << "tex1 = " << tex1 << "   tex2 = " << tex2 << "   tex3 = " << tex3 << std::endl;

    // --- 12-апта, 3-қадам: бірлік нөмірлері. glUseProgram — АЛДЫМЕН! ---
    // Нөмір әріппен (0, 1, 2) жазылады, текстура ID-і емес.
    glUseProgram(shader);
    glUniform1i(glGetUniformLocation(shader, "texture1"), 0);
    glUniform1i(glGetUniformLocation(shader, "texture2"), 1);
    glUniform1i(glGetUniformLocation(shader, "texture3"), 2);

    double lastFrame  = glfwGetTime();
    double lastTitle  = 0.0;
    int    frame      = 0;
    int    shotIndex  = 0;
    int    shotLoaded = -1;
    bool   wrongIdApplied = false;

    while (!glfwWindowShouldClose(window)) {
        double currentFrame = glfwGetTime();
        float  dt = (float)(currentFrame - lastFrame);
        lastFrame = currentFrame;

        // ------------------------- ЖАҢАРТУ ---------------------------
        if (screenshotMode) {
            const ShotStep& s = SHOTS[shotIndex];
            if (shotLoaded != shotIndex) {
                st = BlendState();
                s.setup(st);
                if (st.brokenUnits) clearUnitsRequest = true;
                shotLoaded = shotIndex;
            }
            angle = s.angle;
            logoAngle = st.rotateLogo ? 0.6f : 0.0f;
            camPos = s.pos; yaw = s.yaw; pitch = s.pitch; fov = s.fov;
            updateCameraFront();
        } else {
            angle += dt;
            processInput(window, dt);
            // Қ2: blend уақыт бойынша (пернемен басқаруды B ауыстырады)
            if (st.autoBlend)
                st.blend = 0.5f + 0.5f * sin((float)glfwGetTime());
            if (st.rotateLogo) logoAngle += dt;
        }

        glm::mat4 view = glm::lookAt(camPos, camPos + camFront, camUp);
        glm::mat4 projection = glm::perspective(glm::radians(fov),
            (float)gWidth / (float)gHeight, 0.1f, 100.0f);

        if (currentFrame - lastTitle > 0.25) {   // WSLg-де тақырыпты әр кадрда жаңартпаймыз
            lastTitle = currentFrame;
            char buf[200];
            std::snprintf(buf, sizeof(buf), "%d: %s | blend %.2f%s%s%s%s",
                          st.mode + 1, MODE_NAMES[st.mode], st.blend,
                          st.brokenUnits ? " | glActiveTexture ЖОҚ" : "",
                          st.wrongId ? " | ID ҚАТЕСІ" : "",
                          st.autoBlend ? " | авто" : "",
                          st.perFace ? " | әр жақ" : "");
            glfwSetWindowTitle(window, buf);
        }

        // --------------------------- СЫЗУ ----------------------------
        glClearColor(0.10f, 0.12f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shader);
        glUniformMatrix4fv(tProj, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(tView, 1, GL_FALSE, glm::value_ptr(view));
        // 5-қадам: blend әр кадрда (цикл ішінде) жіберіледі
        glUniform1f(tBlend, st.blend);
        glUniform1i(tMode, st.mode);
        glUniform1f(tAngle, logoAngle);

        // 2-тапсырма: X басылса texture2-ге бірлік нөмірі (1) емес, ID (tex2) беріледі
        if (st.wrongId != wrongIdApplied) {
            if (st.wrongId)
                glUniform1i(tTex2, tex2);   // ӘДЕЙІ ҚАТЕ
            else
                glUniform1i(tTex2, 1);      // дұрысы
            wrongIdApplied = st.wrongId;
        }

        // 4-тапсырмада екінші текстура pattern.png-ден logo.png-ге ауысады
        unsigned int second = (st.mode == MODE_DECAL) ? tex3 : tex2;

        if (clearUnitsRequest) {
            // Бағдарлама glActiveTexture-сыз басталғандағы күй: 1, 2-бірліктер бос
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
            clearUnitsRequest = false;
        }

        // --- 12-апта, 4-қадам: әр кадр байлау ---
        if (!st.brokenUnits) {
            glActiveTexture(GL_TEXTURE0);           // glActiveTexture — glBindTexture-дан БҰРЫН
            glBindTexture(GL_TEXTURE_2D, tex1);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, second);
            // Қ1: үшінші текстура тек 8-тәсілде. Басқа кезде 2-бірлік бос —
            // 2-тапсырмада (texture2 = 2) осы айырмашылық көрінеді.
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, st.mode == MODE_THREE ? tex3 : 0);
            glActiveTexture(GL_TEXTURE0);           // келесі кадрға таза бастау
        } else {
            // 1-тапсырма: екі glActiveTexture жолы "комментарийде"
            // glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex1);
            // glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, second);   // tex1-ді 0-бірліктен ығыстырады
        }

        glBindVertexArray(cubeVao);
        auto drawCube = [&](const glm::mat4& model) {
            glUniformMatrix4fv(tModel, 1, GL_FALSE, glm::value_ptr(model));
            if (st.perFace) {
                // Қ3: алты жақ — алты түрлі коэффициент 0, 0.2, … 1.0
                for (int f = 0; f < 6; ++f) {
                    glUniform1f(tBlend, f / 5.0f);
                    glDrawArrays(GL_TRIANGLES, f * 6, 6);
                }
                glUniform1f(tBlend, st.blend);
            } else {
                glDrawArrays(GL_TRIANGLES, 0, 36);
            }
        };

        for (int i = 0; i < CUBE_COUNT; i++) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), cubePositions[i]);
            model = glm::rotate(model, angle * (0.5f + i * 0.3f), glm::vec3(0.3f, 1.0f, 0.0f));
            drawCube(model);
        }
        for (int i = 0; i < RING_N; i++) {
            float a = glm::radians(360.0f / RING_N * i);
            glm::vec3 p(RING_R * cos(a), FLOOR_Y + 0.5f + (i % 3), RING_R * sin(a));
            glm::mat4 model = glm::translate(glm::mat4(1.0f), p);
            model = glm::rotate(model, angle * 0.4f, glm::vec3(0.0f, 1.0f, 0.0f));
            drawCube(model);
        }

        // Еден-торы мен прицел — түсті шейдермен
        glUseProgram(colorShader);
        glm::mat4 identity(1.0f);
        glUniformMatrix4fv(cProj, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(cView, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(cModel, 1, GL_FALSE, glm::value_ptr(identity));
        glBindVertexArray(gridVao);
        glDrawArrays(GL_LINES, 0, gridVerts);

        float aspect = (float)gWidth / (float)gHeight;
        glm::mat4 crossModel = glm::scale(identity, glm::vec3(1.0f / aspect, 1.0f, 1.0f));
        glUniformMatrix4fv(cModel, 1, GL_FALSE, glm::value_ptr(crossModel));
        glUniformMatrix4fv(cView,  1, GL_FALSE, glm::value_ptr(identity));
        glUniformMatrix4fv(cProj,  1, GL_FALSE, glm::value_ptr(identity));
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(crossVao);
        glDrawArrays(GL_LINES, 0, 4);
        glEnable(GL_DEPTH_TEST);

        if (screenshotMode && ++frame % 10 == 0) {
            saveScreenshot(std::string(SCREENSHOT_DIR) + "/" + SHOTS[shotIndex].file);
            if (++shotIndex == SHOT_COUNT) glfwSetWindowShouldClose(window, true);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &cubeVao);
    glDeleteVertexArrays(1, &gridVao);
    glDeleteVertexArrays(1, &crossVao);
    glDeleteBuffers(1, &cubeVbo);
    glDeleteBuffers(1, &gridVbo);
    glDeleteBuffers(1, &crossVbo);
    GLuint texs[3] = { tex1, tex2, tex3 };
    glDeleteTextures(3, texs);
    glDeleteProgram(shader);
    glDeleteProgram(colorShader);

    glfwTerminate();
    return 0;
}
