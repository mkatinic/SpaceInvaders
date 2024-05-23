#include <iostream>
#include <cstdio>
#include <cstdint>
#include <limits>
#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

bool gameRunning = false;
int moveDir = 0; 
bool firePressed = 0;

#define GL_ERROR_CASE(glerror)\
    case glerror: snprintf(error, sizeof(error), "%s", #glerror)

inline void gl_debug(const char* file, int line) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        char error[128];

        switch (err) {
            GL_ERROR_CASE(GL_INVALID_ENUM); break;
            GL_ERROR_CASE(GL_INVALID_VALUE); break;
            GL_ERROR_CASE(GL_INVALID_OPERATION); break;
            GL_ERROR_CASE(GL_INVALID_FRAMEBUFFER_OPERATION); break;
            GL_ERROR_CASE(GL_OUT_OF_MEMORY); break;
        default: snprintf(error, sizeof(error), "%s", "UNKNOWN_ERROR"); break;
        }

        std::cerr << error << " _ " << file << ":" << line << std::endl;
    }
}

#undef GL_ERROR_CASE

void validateShader(GLuint shader, const char* file = 0) {
    static const unsigned int BUFFER_SIZE = 512;
    char buffer[BUFFER_SIZE];
    GLsizei length = 0;

    glGetShaderInfoLog(shader, BUFFER_SIZE, &length, buffer);

    if (length > 0) {
        if (file == nullptr) 
        {
            std::cout << "Shader " << shader << " compile error: " << buffer << std::endl;
        }
        else 
        {
            std::cout << "Shader " << shader << "(" << file << ") compile error: " << buffer << std::endl;
        }
    }
}

bool validateProgram(GLuint program) {
    static const GLsizei BUFFER_SIZE = 512;
    GLchar buffer[BUFFER_SIZE];
    GLsizei length = 0;

    glGetProgramInfoLog(program, BUFFER_SIZE, &length, buffer);

    if (length > 0) {
        std::cout << "Program " << program << " link error: " << buffer << std::endl;
        return false;
    }

    return true;
}

uint32_t xorshift32(uint32_t* rng)
{
    uint32_t x = *rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

double random(uint32_t* rng)
{
    return (double)xorshift32(rng) / std::numeric_limits<uint32_t>::max();
}

// Will be passed to the GPU to draw on the screen
struct Buffer
{
    size_t width, height;
    uint32_t* data;
};

struct Sprite
{
    size_t width, height;
    uint8_t* data;
};

struct Alien
{
    size_t x, y;
    uint8_t type;
};

struct Player
{
    size_t x, y;
    size_t life;
};

struct Projectile
{
    size_t x, y;
    int dir;
};

#define GAME_MAX_PROJECTILES 128

struct Game
{
    size_t width, height;
    size_t numAliens;
    size_t numProjectiles;
    Alien* aliens;
    Player player;
    Projectile projectiles[GAME_MAX_PROJECTILES];
};

struct SpriteAnimation
{
    bool loop;
    size_t numFrames;
    size_t frameDuration; 
    size_t time;
    Sprite** frames;
};

enum AlienType : uint8_t
{
    ALIEN_DEAD = 0,
    ALIEN_TYPE_A = 1,
    ALIEN_TYPE_B = 2,
    ALIEN_TYPE_C = 3
};

uint32_t rgbToUint32(uint8_t r, uint8_t g, uint8_t b)
{
    return (r << 24) | (g << 16) | (b << 8) | 255;
}

void clearBuffer(Buffer* buffer, uint32_t color)
{
    for (size_t i = 0; i < buffer->width * buffer->height; i++)
    {
        buffer->data[i] = color;
    }
}

bool spriteOverlapCheck(const Sprite& sp_a, size_t x_a, size_t y_a, const Sprite& sp_b, size_t x_b, size_t y_b)
{
    /* NOTE: For simplicity we just check for overlap of the sprite
    rectangles. Instead, if the rectangles overlap, we should
    further check if any pixel of sprite A overlap with any of
    sprite B. */
    if (x_a < x_b + sp_b.width && x_a + sp_a.width > x_b && y_a < y_b + sp_b.height && y_a + sp_a.height > y_b)
    {
        return true;
    }

    return false;
}

void drawSprite(Buffer* buffer, const Sprite& sprite, size_t x, size_t y, uint32_t color)
{
    for (size_t xidx = 0; xidx < sprite.width; ++xidx)
    {
        for (size_t yidx = 0; yidx < sprite.height; ++yidx)
        {
            size_t sy = sprite.height - 1 + y - yidx;
            size_t sx = x + xidx;
            if (sprite.data[yidx * sprite.width + xidx] &&
                sy < buffer->height && sx < buffer->width)
            {
                buffer->data[sy * buffer->width + sx] = color;
            }
        }
    }
}

void drawNumber(Buffer* buffer, const Sprite& numberSpritesheet, size_t number, size_t x, size_t y, uint32_t color)
{
    uint8_t digits[64];
    size_t num_digits = 0;

    size_t current_number = number;
    do
    {
        digits[num_digits++] = current_number % 10;
        current_number = current_number / 10;
    } while (current_number > 0);

    size_t xp = x;
    size_t stride = numberSpritesheet.width * numberSpritesheet.height;
    Sprite sprite = numberSpritesheet;
    for (size_t i = 0; i < num_digits; i++)
    {
        uint8_t digit = digits[num_digits - i - 1];
        sprite.data = numberSpritesheet.data + digit * stride;
        drawSprite(buffer, sprite, xp, y, color);
        xp += sprite.width + 1;
    }
}

void drawText(Buffer* buffer, const Sprite& textSpritesheet, const char* text, size_t x, size_t y, uint32_t color)
{
    size_t xp = x;
    size_t stride = textSpritesheet.width * textSpritesheet.height;
    Sprite sprite = textSpritesheet;
    for (const char* charp = text; *charp != '\0'; charp++)
    {
        char character = *charp - 32;
        if (character < 0 || character >= 65) continue;

        sprite.data = textSpritesheet.data + character * stride;
        drawSprite(buffer, sprite, xp, y, color);
        xp += sprite.width + 1;
    }
}

// Simple error callback 
void error_callback(int error, const char* description)
{
    std::cerr << "Error: " << description << std::endl;
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    switch (key) {
    case GLFW_KEY_ESCAPE:
        if (action == GLFW_PRESS) 
            gameRunning = false;
        break;
    case GLFW_KEY_D:
        if (action == GLFW_PRESS) 
            moveDir += 1;
        else if (action == GLFW_RELEASE) 
            moveDir -= 1;
        break;
    case GLFW_KEY_A:
        if (action == GLFW_PRESS) 
            moveDir -= 1;
        else if (action == GLFW_RELEASE) 
            moveDir += 1;
        break;
    case GLFW_KEY_SPACE:
        if (action == GLFW_RELEASE) 
            firePressed = true;
        break;
    default:
        break;
    }
}

int main(int argc, char* argv[])
{
    const size_t bufferWidth = 224;
    const size_t bufferHeight = 256;

    // Sets the error callback 
    glfwSetErrorCallback(error_callback);

    GLFWwindow* window;

    if (!glfwInit()) 
        return -1;

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    // Create a windowed mode window and its OpenGL context 
    window = glfwCreateWindow(640, 480, "Space Invaders", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    glfwSetKeyCallback(window, keyCallback);

    glfwMakeContextCurrent(window);

    // Initializing GLEW 
    GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        std::cerr << "Error initializing GLEW" << std::endl;
        glfwTerminate();
        return -1;
    }
    int glVersion[2] = { -1, 1 };
    glGetIntegerv(GL_MAJOR_VERSION, &glVersion[0]);
    glGetIntegerv(GL_MINOR_VERSION, &glVersion[1]);

    gl_debug(__FILE__, __LINE__);

    std::cout << "Using OpenGL: " << glVersion[0] << "." << glVersion[1] << std::endl;
    std::cout << "Renderer used: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "Shading Language: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

    // Enablign VSync
    glfwSwapInterval(1);

    glClearColor(1.0, 0.0, 0.0, 1.0);

    // Creates graphics buffer
    Buffer buffer;
    buffer.width = bufferWidth;
    buffer.height = bufferHeight; 
    buffer.data = new uint32_t[buffer.width * buffer.height];

    clearBuffer(&buffer, 0);

    // Generating the texture for presenting buffer to OpenGL
    GLuint bufferTexture;
    glGenTextures(1, &bufferTexture);
    glBindTexture(GL_TEXTURE_2D, bufferTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, buffer.width, buffer.height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, buffer.data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Vertex array object 
    GLuint fullscreenTriangleVao;
    glGenVertexArrays(1, &fullscreenTriangleVao);

    // Shaders to display the buffer 
    static const char* fragmentShader =
        "\n"
        "#version 330\n"
        "\n"
        "uniform sampler2D buffer;\n"
        "noperspective in vec2 TexCoord;\n"
        "\n"
        "out vec3 outColor;\n"
        "\n"
        "void main(void){\n"
        "    outColor = texture(buffer, TexCoord).rgb;\n"
        "}\n";

    const char* vertexShader =
        "\n"
        "#version 330\n"
        "\n"
        "noperspective out vec2 TexCoord;\n"
        "\n"
        "void main(void){\n"
        "\n"
        "    TexCoord.x = (gl_VertexID == 2)? 2.0: 0.0;\n"
        "    TexCoord.y = (gl_VertexID == 1)? 2.0: 0.0;\n"
        "    \n"
        "    gl_Position = vec4(2.0 * TexCoord - 1.0, 0.0, 1.0);\n"
        "}\n";

    GLuint shaderId = glCreateProgram();

    {
        // Create vertex shader to be compiled 
        GLuint shaderVp = glCreateShader(GL_VERTEX_SHADER);

        glShaderSource(shaderVp, 1, &vertexShader, 0);
        glCompileShader(shaderVp);
        validateShader(shaderVp, vertexShader);
        glAttachShader(shaderId, shaderVp);

        glDeleteShader(shaderVp);
    }

    {
        // Create fragment shader to be compiled 
        GLuint shaderFp = glCreateShader(GL_FRAGMENT_SHADER);

        glShaderSource(shaderFp, 1, &fragmentShader, 0);
        glCompileShader(shaderFp);
        validateShader(shaderFp, fragmentShader);
        glAttachShader(shaderId, shaderFp);

        glDeleteShader(shaderFp);
    }

    glLinkProgram(shaderId);

    if (!validateProgram(shaderId))
    {
        std::cerr << "Error while validating the shader." << std::endl;
        glfwTerminate();
        glDeleteVertexArrays(1, &fullscreenTriangleVao);
        delete[] buffer.data;
        return -1;
    }

    glUseProgram(shaderId);

    GLint location = glGetUniformLocation(shaderId, "buffer");
    glUniform1i(location, 0);

    // OpenGL setup 
    glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(fullscreenTriangleVao);

    Sprite alienSprites[6];

    alienSprites[0].width = 8;
    alienSprites[0].height = 8;
    alienSprites[0].data = new uint8_t[64]
    {
        0,0,0,1,1,0,0,0, // ...@@...
        0,0,1,1,1,1,0,0, // ..@@@@..
        0,1,1,1,1,1,1,0, // .@@@@@@.
        1,1,0,1,1,0,1,1, // @@.@@.@@
        1,1,1,1,1,1,1,1, // @@@@@@@@
        0,1,0,1,1,0,1,0, // .@.@@.@.
        1,0,0,0,0,0,0,1, // @......@
        0,1,0,0,0,0,1,0  // .@....@.
    };

    alienSprites[1].width = 8;
    alienSprites[1].height = 8;
    alienSprites[1].data = new uint8_t[64]
    {
        0,0,0,1,1,0,0,0, // ...@@...
        0,0,1,1,1,1,0,0, // ..@@@@..
        0,1,1,1,1,1,1,0, // .@@@@@@.
        1,1,0,1,1,0,1,1, // @@.@@.@@
        1,1,1,1,1,1,1,1, // @@@@@@@@
        0,0,1,0,0,1,0,0, // ..@..@..
        0,1,0,1,1,0,1,0, // .@.@@.@.
        1,0,1,0,0,1,0,1  // @.@..@.@
    };

    alienSprites[2].width = 11;
    alienSprites[2].height = 8;
    alienSprites[2].data = new uint8_t[88]
    {
        0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
        0,0,0,1,0,0,0,1,0,0,0, // ...@...@...
        0,0,1,1,1,1,1,1,1,0,0, // ..@@@@@@@..
        0,1,1,0,1,1,1,0,1,1,0, // .@@.@@@.@@.
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        1,0,1,1,1,1,1,1,1,0,1, // @.@@@@@@@.@
        1,0,1,0,0,0,0,0,1,0,1, // @.@.....@.@
        0,0,0,1,1,0,1,1,0,0,0  // ...@@.@@...
    };

    alienSprites[3].width = 11;
    alienSprites[3].height = 8;
    alienSprites[3].data = new uint8_t[88]
    {
        0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
        1,0,0,1,0,0,0,1,0,0,1, // @..@...@..@
        1,0,1,1,1,1,1,1,1,0,1, // @.@@@@@@@.@
        1,1,1,0,1,1,1,0,1,1,1, // @@@.@@@.@@@
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        0,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@.
        0,0,1,0,0,0,0,0,1,0,0, // ..@.....@..
        0,1,0,0,0,0,0,0,0,1,0  // .@.......@.
    };

    alienSprites[4].width = 12;
    alienSprites[4].height = 8;
    alienSprites[4].data = new uint8_t[96]
    {
        0,0,0,0,1,1,1,1,0,0,0,0, // ....@@@@....
        0,1,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@@.
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        1,1,1,0,0,1,1,0,0,1,1,1, // @@@..@@..@@@
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        0,0,0,1,1,0,0,1,1,0,0,0, // ...@@..@@...
        0,0,1,1,0,1,1,0,1,1,0,0, // ..@@.@@.@@..
        1,1,0,0,0,0,0,0,0,0,1,1  // @@........@@
    };


    alienSprites[5].width = 12;
    alienSprites[5].height = 8;
    alienSprites[5].data = new uint8_t[96]
    {
        0,0,0,0,1,1,1,1,0,0,0,0, // ....@@@@....
        0,1,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@@.
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        1,1,1,0,0,1,1,0,0,1,1,1, // @@@..@@..@@@
        1,1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@@
        0,0,1,1,1,0,0,1,1,1,0,0, // ..@@@..@@@..
        0,1,1,0,0,1,1,0,0,1,1,0, // .@@..@@..@@.
        0,0,1,1,0,0,0,0,1,1,0,0  // ..@@....@@..
    };

    Sprite alienDeathSprite;
    alienDeathSprite.width = 13;
    alienDeathSprite.height = 7;
    alienDeathSprite.data = new uint8_t[91]
    {
        0,1,0,0,1,0,0,0,1,0,0,1,0, // .@..@...@..@.
        0,0,1,0,0,1,0,1,0,0,1,0,0, // ..@..@.@..@..
        0,0,0,1,0,0,0,0,0,1,0,0,0, // ...@.....@...
        1,1,0,0,0,0,0,0,0,0,0,1,1, // @@.........@@
        0,0,0,1,0,0,0,0,0,1,0,0,0, // ...@.....@...
        0,0,1,0,0,1,0,1,0,0,1,0,0, // ..@..@.@..@..
        0,1,0,0,1,0,0,0,1,0,0,1,0  // .@..@...@..@.
    };

    Sprite playerSprite;
    playerSprite.width = 11;
    playerSprite.height = 7;
    playerSprite.data = new uint8_t[77]
    {
        0,0,0,0,0,1,0,0,0,0,0, // .....@.....
        0,0,0,0,1,1,1,0,0,0,0, // ....@@@....
        0,0,0,0,1,1,1,0,0,0,0, // ....@@@....
        0,1,1,1,1,1,1,1,1,1,0, // .@@@@@@@@@.
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
        1,1,1,1,1,1,1,1,1,1,1, // @@@@@@@@@@@
    };

    Sprite textSpritesheet;
    textSpritesheet.width = 5;
    textSpritesheet.height = 7;
    textSpritesheet.data = new uint8_t[65 * 35]
    {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,
        0,1,0,1,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,0,1,0,0,1,0,1,0,1,1,1,1,1,0,1,0,1,0,1,1,1,1,1,0,1,0,1,0,0,1,0,1,0,
        0,0,1,0,0,0,1,1,1,0,1,0,1,0,0,0,1,1,1,0,0,0,1,0,1,0,1,1,1,0,0,0,1,0,0,
        1,1,0,1,0,1,1,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,1,1,0,1,0,1,1,
        0,1,1,0,0,1,0,0,1,0,1,0,0,1,0,0,1,1,0,0,1,0,0,1,0,1,0,0,0,1,0,1,1,1,1,
        0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,
        1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,
        0,0,1,0,0,1,0,1,0,1,0,1,1,1,0,0,0,1,0,0,0,1,1,1,0,1,0,1,0,1,0,0,1,0,0,
        0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,1,1,1,1,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,

        0,1,1,1,0,1,0,0,0,1,1,0,0,1,1,1,0,1,0,1,1,1,0,0,1,1,0,0,0,1,0,1,1,1,0,
        0,0,1,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,1,1,0,
        0,1,1,1,0,1,0,0,0,1,0,0,0,0,1,0,0,1,1,0,0,1,0,0,0,1,0,0,0,0,1,1,1,1,1,
        1,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0,0,0,0,1,1,0,0,0,1,0,1,1,1,0,
        0,0,0,1,0,0,0,1,1,0,0,1,0,1,0,1,0,0,1,0,1,1,1,1,1,0,0,0,1,0,0,0,0,1,0,
        1,1,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,1,0,0,0,0,1,1,0,0,0,1,0,1,1,1,0,
        0,1,1,1,0,1,0,0,0,1,1,0,0,0,0,1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0,
        1,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,
        0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0,
        0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,0,1,1,1,1,0,0,0,0,1,1,0,0,0,1,0,1,1,1,0,

        0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,
        0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,
        0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
        1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,
        0,1,1,1,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,
        0,1,1,1,0,1,0,0,0,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,0,1,0,0,0,1,0,1,1,1,0,

        0,0,1,0,0,0,1,0,1,0,1,0,0,0,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,1,1,0,0,0,1,
        1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0,
        0,1,1,1,0,1,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,1,1,1,0,
        1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0,
        1,1,1,1,1,1,0,0,0,0,1,0,0,0,0,1,1,1,1,0,1,0,0,0,0,1,0,0,0,0,1,1,1,1,1,
        1,1,1,1,1,1,0,0,0,0,1,0,0,0,0,1,1,1,1,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,
        0,1,1,1,0,1,0,0,0,1,1,0,0,0,0,1,0,1,1,1,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0,
        1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,
        0,1,1,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,1,1,0,
        0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,1,0,1,1,1,0,
        1,0,0,0,1,1,0,0,1,0,1,0,1,0,0,1,1,0,0,0,1,0,1,0,0,1,0,0,1,0,1,0,0,0,1,
        1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,1,1,1,
        1,0,0,0,1,1,1,0,1,1,1,0,1,0,1,1,0,1,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,
        1,0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,0,1,0,1,1,0,0,1,1,1,0,0,0,1,1,0,0,0,1,
        0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0,
        1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,
        0,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,1,0,1,1,0,0,1,1,0,1,1,1,1,
        1,1,1,1,0,1,0,0,0,1,1,0,0,0,1,1,1,1,1,0,1,0,1,0,0,1,0,0,1,0,1,0,0,0,1,
        0,1,1,1,0,1,0,0,0,1,1,0,0,0,0,0,1,1,1,0,1,0,0,0,1,0,0,0,0,1,0,1,1,1,0,
        1,1,1,1,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,
        1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,0,1,1,1,0,
        1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,
        1,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,1,0,1,1,0,1,0,1,1,1,0,1,1,1,0,0,0,1,
        1,0,0,0,1,1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0,1,0,0,0,1,1,0,0,0,1,
        1,0,0,0,1,1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,
        1,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,1,1,1,1,

        0,0,0,1,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,1,
        0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,
        1,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,1,1,0,0,0,
        0,0,1,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
        0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };

    Sprite numberSpritesheet = textSpritesheet;
    numberSpritesheet.data += 16 * 35;

    Sprite playerProjectileSprite;
    playerProjectileSprite.width = 1;
    playerProjectileSprite.height = 3;
    playerProjectileSprite.data = new uint8_t[3]
    {
        1, // @
        1, // @
        1  // @
    };

    Sprite alienProjectileSprite[2];
    alienProjectileSprite[0].width = 3;
    alienProjectileSprite[0].height = 7;
    alienProjectileSprite[0].data = new uint8_t[21]
    {
        0,1,0, // .@.
        1,0,0, // @..
        0,1,0, // .@.
        0,0,1, // ..@
        0,1,0, // .@.
        1,0,0, // @..
        0,1,0, // .@.
    };

    alienProjectileSprite[1].width = 3;
    alienProjectileSprite[1].height = 7;
    alienProjectileSprite[1].data = new uint8_t[21]
    {
        0,1,0, // .@.
        0,0,1, // ..@
        0,1,0, // .@.
        1,0,0, // @..
        0,1,0, // .@.
        0,0,1, // ..@
        0,1,0, // .@.
    };

    // Create the animations and assign it sprites 
    SpriteAnimation alienProjectileAnimation;
    alienProjectileAnimation.loop = true;
    alienProjectileAnimation.numFrames = 2;
    alienProjectileAnimation.frameDuration = 5;
    alienProjectileAnimation.time = 0;

    alienProjectileAnimation.frames = new Sprite * [2];
    alienProjectileAnimation.frames[0] = &alienProjectileSprite[0];
    alienProjectileAnimation.frames[1] = &alienProjectileSprite[1];

    SpriteAnimation alienAnimation[3];

    size_t alienUpdateFrequency = 120;

    for (size_t i = 0; i < 3; i++)
    {
        alienAnimation[i].loop = true;
        alienAnimation[i].numFrames = 2;
        alienAnimation[i].frameDuration = alienUpdateFrequency;
        alienAnimation[i].time = 0;

        alienAnimation[i].frames = new Sprite * [2];
        alienAnimation[i].frames[0] = &alienSprites[2 * i];
        alienAnimation[i].frames[1] = &alienSprites[2 * i + 1];
    }

    // Initializing the game 
    Game game{};
    game.width = buffer.width;
    game.height = buffer.height;
    game.numAliens = 55;
    game.aliens = new Alien[game.numAliens];

    game.player.x = 112 - 5;
    game.player.y = 32;
    game.player.life = 3;

    size_t alienSwarmPosition = 24;
    size_t alienSwarmMaxPosition = game.width - 16 * 11 - 3;

    size_t aliensKilled = 0;
    size_t alienUpdateTimer = 0;
    bool shouldChangeSpeed = false;

    for (size_t yidx = 0; yidx < 5; yidx++)
    {
        for (size_t xidx = 0; xidx < 11; xidx++)
        {
            Alien& alien = game.aliens[yidx * 11 + xidx];
            alien.type = (5 - yidx) / 2 + 1;

            const Sprite& sprite = alienSprites[2 * (alien.type - 1)];

            alien.x = 16 * xidx + 20 + (alienDeathSprite.width - sprite.width) / 2;
            alien.y = 17 * yidx + 128;
        }
    }

    uint8_t* deathCounters = new uint8_t[game.numAliens];
    for (size_t i = 0; i < game.numAliens; i++)
    {
        deathCounters[i] = 10;
    }

    uint32_t clearColor = rgbToUint32(0, 0, 0);
    uint32_t rng = 13;

    int alienMoveDir = 4;

    size_t score = 0;
    size_t credits = 0;

    gameRunning = true;
    int playerMoveDir = 0;
    while (!glfwWindowShouldClose(window) && gameRunning)
    {
        // Render the game here 
        clearBuffer(&buffer, clearColor);

        if (game.player.life == 0)
        {

            drawText(&buffer, textSpritesheet, "GAME OVER", game.width / 2 - 30, game.height / 2, rgbToUint32(128, 0, 0));
            drawText(&buffer, textSpritesheet, "SCORE", 4, game.height - textSpritesheet.height - 7, rgbToUint32(128, 0, 0));
            drawNumber(&buffer, numberSpritesheet, score, 4 + 2 * numberSpritesheet.width, game.height - 2 * numberSpritesheet.height - 12, rgbToUint32(128, 0, 0));

            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, buffer.width, buffer.height, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,  buffer.data);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            glfwSwapBuffers(window);
            glfwPollEvents();
            continue;
        }

        drawText(&buffer, textSpritesheet, "SCORE", 4, game.height - textSpritesheet.height - 7, rgbToUint32(0, 255, 0));
        drawNumber(&buffer, numberSpritesheet, score, 4 + 2 * numberSpritesheet.width, game.height - 2 * numberSpritesheet.height - 12, rgbToUint32(0, 255, 0));

        {
            char credit_text[16];
            sprintf_s(credit_text, "CREDIT %02lu", credits);
            drawText(&buffer, textSpritesheet, credit_text, 164, 7, rgbToUint32(0, 255, 0));
        }

        drawNumber(&buffer, numberSpritesheet, game.player.life, 4, 7, rgbToUint32(0, 255, 0));
        size_t xp = 11 + numberSpritesheet.width;
        for (size_t i = 0; i < game.player.life - 1; i++)
        {
            drawSprite(&buffer, playerSprite, xp, 7, rgbToUint32(0, 255, 0));
            xp += playerSprite.width + 2;
        }

        for (size_t i = 0; i < game.width; i++)
        {
            buffer.data[game.width * 16 + i] = rgbToUint32(0, 255, 0);
        }

        // Drawing the aliens 
        for (size_t aidx = 0; aidx < game.numAliens; aidx++)
        {
            if (!deathCounters[aidx]) 
                continue;

            const Alien& alien = game.aliens[aidx];
            if (alien.type == ALIEN_DEAD)
            {
                drawSprite(&buffer, alienDeathSprite, alien.x, alien.y, rgbToUint32(255, 255, 255));
            }
            else
            {
                const SpriteAnimation& animation = alienAnimation[alien.type - 1];
                size_t current_frame = animation.time / animation.frameDuration;
                const Sprite& sprite = *animation.frames[current_frame];
                drawSprite(&buffer, sprite, alien.x, alien.y, rgbToUint32(255, 255, 255));
            }
        }

        // Draw the projectiles 
        for (size_t pidx = 0; pidx < game.numProjectiles; pidx++)
        {
            const Projectile& projectile = game.projectiles[pidx];
            const Sprite* sprite;
            if (projectile.dir > 0)
                sprite = &playerProjectileSprite;
            else
            {
                size_t cf = alienProjectileAnimation.time / alienProjectileAnimation.frameDuration;
                sprite = &alienProjectileSprite[cf];
            }
            if (projectile.dir > 0)
                drawSprite(&buffer, *sprite, projectile.x, projectile.y, rgbToUint32(0, 255, 0));
            else
                drawSprite(&buffer, *sprite, projectile.x, projectile.y, rgbToUint32(255, 255, 255));
        }

        drawSprite(&buffer, playerSprite, game.player.x, game.player.y, rgbToUint32(0, 255, 0));

        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, buffer.width, buffer.height, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, buffer.data);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glfwSwapBuffers(window);

        // Simulate projectile 
        for (size_t pidx = 0; pidx < game.numProjectiles;)
        {
            game.projectiles[pidx].y += game.projectiles[pidx].dir;
            if (game.projectiles[pidx].y >= game.height || game.projectiles[pidx].y < playerProjectileSprite.height)
            {
                game.projectiles[pidx] = game.projectiles[game.numProjectiles - 1];
                game.numProjectiles--;
                continue;
            }

            // Alien bullet
            if (game.projectiles[pidx].dir < 0)
            {
                bool overlap = spriteOverlapCheck(alienProjectileSprite[0], game.projectiles[pidx].x, game.projectiles[pidx].y, playerSprite, game.player.x, game.player.y);
                if (overlap)
                {
                    game.player.life--;
                    game.projectiles[pidx] = game.projectiles[game.numProjectiles - 1];
                    game.numProjectiles--;
                    break;
                }
            }
            // Player bullet
            else
            {
                // Check if player bullet hits an alien bullet
                for (size_t pjidx = 0; pjidx < game.numProjectiles; pjidx++)
                {
                    if (pidx == pjidx) 
                        continue;

                    bool overlap = spriteOverlapCheck(playerProjectileSprite, game.projectiles[pidx].x, game.projectiles[pidx].y, alienProjectileSprite[0], game.projectiles[pjidx].x, game.projectiles[pjidx].y);

                    if (overlap)
                    {
                        if (pjidx == game.numProjectiles - 1)
                        {
                            game.projectiles[pidx] = game.projectiles[game.numProjectiles - 2];
                        }
                        else if (pidx == game.numProjectiles - 1)
                        {
                            game.projectiles[pjidx] = game.projectiles[game.numProjectiles - 2];
                        }
                        else
                        {
                            game.projectiles[(pidx < pjidx) ? pidx : pjidx] = game.projectiles[game.numProjectiles - 1];
                            game.projectiles[(pidx < pjidx) ? pjidx : pidx] = game.projectiles[game.numProjectiles - 2];
                        }
                        game.numProjectiles -= 2;
                        break;
                    }
                }

                // Check hit
                for (size_t aidx = 0; aidx < game.numAliens; aidx++)
                {
                    const Alien& alien = game.aliens[aidx];
                    if (alien.type == ALIEN_DEAD) 
                        continue;

                    const SpriteAnimation& animation = alienAnimation[alien.type - 1];
                    size_t currentFrame = animation.time / animation.frameDuration;
                    const Sprite& alienSprite = *animation.frames[currentFrame];
                    bool projectileOverlap = spriteOverlapCheck(playerProjectileSprite, game.projectiles[pidx].x, game.projectiles[pidx].y, alienSprite, alien.x, alien.y);

                    if (projectileOverlap)
                    {
                        score += 10 * (4 - game.aliens[aidx].type);
                        game.aliens[aidx].type = ALIEN_DEAD;
                        // NOTE: Hack to recenter death sprite
                        game.aliens[aidx].x -= (alienDeathSprite.width - alienSprite.width) / 2;
                        game.projectiles[pidx] = game.projectiles[game.numProjectiles - 1];
                        game.numProjectiles--;
                        aliensKilled++;

                        if (aliensKilled % 15 == 0) 
                            shouldChangeSpeed = true;

                        break;
                    }      
                }
            }

            pidx++;
        }

        // Check alien and player collision
        for (size_t aidx = 0; aidx < game.numAliens; aidx++)
        {
            const Alien& alien = game.aliens[aidx];
            if (alien.type == ALIEN_DEAD)
                continue;

            const SpriteAnimation& animation = alienAnimation[alien.type - 1];
            size_t currentFrame = animation.time / animation.frameDuration;
            const Sprite& alienSprite = *animation.frames[currentFrame];
            bool collisionOverlap = spriteOverlapCheck(playerSprite, game.player.x, game.player.y, alienSprite, alien.x, alien.y);
            if (collisionOverlap)
            {
                game.player.life--;
                score += 10 * (4 - game.aliens[aidx].type);
                game.aliens[aidx].type = ALIEN_DEAD;
                game.aliens[aidx].x -= (alienDeathSprite.width - alienSprite.width) / 2;
                aliensKilled++;

                if (aliensKilled % 15 == 0)
                    shouldChangeSpeed = true;

                break;
            }
        }

        // Simulate aliens
        if (shouldChangeSpeed)
        {
            shouldChangeSpeed = false;
            alienUpdateFrequency /= 2;
            for (size_t i = 0; i < 3; ++i)
            {
                alienAnimation[i].frameDuration = alienUpdateFrequency;
            }
        }

        // Update death counters
        for (size_t aidx = 0; aidx < game.numAliens; aidx++)
        {
            const Alien& alien = game.aliens[aidx];
            if (alien.type == ALIEN_DEAD && deathCounters[aidx])
            {
                deathCounters[aidx]--;
            }
        }

        if (alienUpdateTimer >= alienUpdateFrequency)
        {
            alienUpdateTimer = 0;

            if ((int)alienSwarmPosition + alienMoveDir < 0)
            {
                alienMoveDir *= -1;
                for (size_t ai = 0; ai < game.numAliens; ai++)
                {
                    Alien& alien = game.aliens[ai];
                    if (alien.y < (game.player.y - 8))
                        alien.y = 188;
                    else
                        alien.y -= 8;
                }
            }
            else if (alienSwarmPosition > alienSwarmMaxPosition - alienMoveDir)
            {
                alienMoveDir *= -1;
            }
            alienSwarmPosition += alienMoveDir;

            for (size_t ai = 0; ai < game.numAliens; ai++)
            {
                Alien& alien = game.aliens[ai];
                alien.x += alienMoveDir;
            }

            if (aliensKilled < game.numAliens)
            {
                size_t rai = game.numAliens * random(&rng);
                while (game.aliens[rai].type == ALIEN_DEAD)
                {
                    rai = game.numAliens * random(&rng);
                }
                const Sprite& alienSprite = *alienAnimation[game.aliens[rai].type - 1].frames[0];
                game.projectiles[game.numProjectiles].x = game.aliens[rai].x + alienSprite.width / 2;
                game.projectiles[game.numProjectiles].y = game.aliens[rai].y - alienProjectileSprite[0].height;
                game.projectiles[game.numProjectiles].dir = -2;
                game.numProjectiles++;
            }
        }

        // Update animations
        for (size_t i = 0; i < 3; i++)
        {
            alienAnimation[i].time++;
            if (alienAnimation[i].time >= alienAnimation[i].numFrames * alienAnimation[i].frameDuration)
            {
                alienAnimation[i].time = 0;
            }
        }
        alienProjectileAnimation.time++;
        if (alienProjectileAnimation.time >= alienProjectileAnimation.numFrames * alienProjectileAnimation.frameDuration)
        {
            alienProjectileAnimation.time = 0;
        }

        alienUpdateTimer++;

        // Simulate the player 
        playerMoveDir = 2 * moveDir;
        if (playerMoveDir != 0)
        {
            if (game.player.x + playerSprite.width + playerMoveDir >= game.width)
            {
                game.player.x = game.width - playerSprite.width;
            }
            else if ((int)game.player.x + playerMoveDir <= 0)
            {
                game.player.x = 0;
            }
            else game.player.x += playerMoveDir;
        }

        if (aliensKilled < game.numAliens)
        {
            size_t ai = 0;
            while (game.aliens[ai].type == ALIEN_DEAD) ai++;
            const Sprite& sprite = alienSprites[2 * (game.aliens[ai].type - 1)];
            size_t pos = game.aliens[ai].x - (alienDeathSprite.width - sprite.width) / 2;
            if (pos > alienSwarmPosition) 
                alienSwarmPosition = pos;

            ai = game.numAliens - 1;
            while (game.aliens[ai].type == ALIEN_DEAD) ai--;
            pos = game.width - game.aliens[ai].x - 13 + pos;
            if (pos > alienSwarmMaxPosition) 
                alienSwarmMaxPosition = pos;
        }
        else
        {
            alienUpdateFrequency = 120;
            alienSwarmPosition = 24;

            aliensKilled = 0;
            alienUpdateTimer = 0;

            alienMoveDir = 4;

            for (size_t xidx = 0; xidx < 11; xidx++)
            {
                for (size_t yidx = 0; yidx < 5; yidx++)
                {
                    size_t aidx = xidx * 5 + yidx;

                    deathCounters[aidx] = 10;

                    Alien& alien = game.aliens[aidx];
                    alien.type = (5 - yidx) / 2 + 1;

                    const Sprite& sprite = alienSprites[2 * (alien.type - 1)];

                    alien.x = 16 * xidx + alienSwarmPosition + (alienDeathSprite.width - sprite.width) / 2;
                    alien.y = 17 * yidx + 128;
                }
            }
        }

        if (firePressed && game.numProjectiles < GAME_MAX_PROJECTILES)
        {
            game.projectiles[game.numProjectiles].x = game.player.x + playerSprite.width / 2;
            game.projectiles[game.numProjectiles].y = game.player.y + playerSprite.height;
            game.projectiles[game.numProjectiles].dir = 2;
            game.numProjectiles++;
        }
        firePressed = false;

        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    glDeleteVertexArrays(1, &fullscreenTriangleVao);

    for (size_t i = 0; i < 6; ++i)
    {
        delete[] alienSprites[i].data;
    }

    delete[] textSpritesheet.data;
    delete[] alienDeathSprite.data;
    delete[] playerProjectileSprite.data;
    delete[] alienProjectileSprite[0].data;
    delete[] alienProjectileSprite[1].data;
    delete[] alienProjectileAnimation.frames;

    for (size_t i = 0; i < 3; ++i)
    {
        delete[] alienAnimation[i].frames;
    }
    delete[] buffer.data;
    delete[] game.aliens;
    delete[] deathCounters;

    return 0;
}