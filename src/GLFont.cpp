#include "GLFont.h"
#include "GLUtils.h"

#include <stdio.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

// GLM
#include "glm\gtc\type_ptr.hpp"
#include "glm\gtx\transform.hpp"

GLFont::GLFont(char* font, int windowWidth, int windowHeight) :
  _windowWidth(windowWidth),
  _windowHeight(windowHeight),
  _isInitialized(false),
  _alignment(FontAlignment::CenterAligned),
  _textColor(0, 0, 0, 1),
  _uniformMVPHandle(-1)
{
    // Initialize FreeType
    _error = FT_Init_FreeType(&_ft);
    if(_error) {
        throw std::exception("Failed to initialize FreeType");
    }

    setFont(font);

    //setPixelSize(36); // default pixel size

    _sx = 2.0 / _windowWidth;
    _sy = 2.0 / _windowHeight;
    
    // Set up model view projection matrices
    //_projection = glm::ortho(-800.0f, 800.0f, -600.0f, 600.0f, 0.1f, 100.0f);
    //_projection = glm::ortho(-200.0f, 200.0f, -200.0f, 200.0f, 0.1f, 100.0f);
    // Since we are dealing with 2D text, we can use an ortho projection matrix 
    _projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);
    //_projection = glm::perspective(45.0f, (float)windowWidth / (float)windowHeight, 0.1f, 100.0f);
    //_projection = glm::perspective(45.0f,           // Field of view (in degrees)
    //                               800.0f / 600.0f, // Aspect ratio 
    //                               0.1f,            // Near clipping distance
    //                               100.0f);         // Far clipping 

    _view = glm::lookAt(glm::vec3(0, 0, 1),  // Camera position in world space
                        glm::vec3(0, 0, 0),  // look at origin
                        glm::vec3(0, 1, 0)); // Head is up (set to 0, -1, 0 to look upside down)

    _model = glm::mat4(1.0f); // Identity matrix

    recalculateMVP();
}

GLFont::~GLFont() {}

void GLFont::init() {
    // Load the shaders
    _programId = glCreateProgram();
    GLUtils::loadShader("shaders\\fontVertex.shader", GL_VERTEX_SHADER, _programId);
    GLUtils::loadShader("shaders\\fontFragment.shader", GL_FRAGMENT_SHADER, _programId);
    
    glUseProgram(_programId);

    // Create and bind the vertex array object
    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);

    // Create the texture
    setPixelSize(48);
    glActiveTexture(GL_TEXTURE0 + _texIds[_pixelSize]);
    //glGenTextures(1, &_texIds[_pixelSize]);
    glBindTexture(GL_TEXTURE_2D, _texIds[_pixelSize]);

    //// Get shader handles
    //_uniformTextureHandle = glGetUniformLocation(_programId, "tex");
    //_uniformTextColorHandle = glGetUniformLocation(_programId, "textColor");
    //_uniformMVPHandle = glGetUniformLocation(_programId, "mvp");

    //glUniform1i(_uniformTextureHandle, 0);
    //glUniform4fv(_uniformTextColorHandle, 1, glm::value_ptr(_textColor));
    //glUniformMatrix4fv(_uniformMVPHandle, 1, GL_FALSE, glm::value_ptr(_mvp));

    //// Set texture parameters
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Create the vertex buffer object
    glGenBuffers(1, &_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    //glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), 0);
    
    glUseProgram(0);

    _isInitialized = true;
}

void GLFont::glPrint(const char *text, float x, float y, float viewWidth, float viewHeight) {
    if(!_isInitialized)
        throw std::exception("Error: you must first initialize GLFont.");

    FT_GlyphSlot slot = _face->glyph;
    std::vector<Point> coords;

    // Align text
    calculateAlignment((const unsigned char*)text, x);

    // Normalize window coordinates
    x = -1 + x * _sx;
    y = 1 - y * _sy;
    
    glUseProgram(_programId);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0 + _texIds[_pixelSize]);

    glBindBuffer(GL_ARRAY_BUFFER, _vbo);

    glBindTexture(GL_TEXTURE_2D, _texIds[_pixelSize]);
    glUniform1i(_uniformTextureHandle, _texIds[_pixelSize]);

    glEnableVertexAttribArray(0);

    for(const char *p = text; *p; ++p) {
        float x2 = x + chars[*p].bitmapLeft * _sx; // scaled x coord
        float y2 = -y - chars[*p].bitmapTop * _sy; // scaled y coord
        float w = chars[*p].bitmapWidth * _sx; // scaled width of character
        float h = chars[*p].bitmapHeight * _sy; // scaled height of character

        // Advance cursor to start of next character
        x += chars[*p].advanceX * _sx;
        y += chars[*p].advanceY * _sy;

        // Skip glyphs with no pixels (e.g. spaces)
        if(!w || !h)
            continue;

        coords.push_back(Point(x2,                // window x
                               -y2,               // window y
                               chars[*p].xOffset, // texture atlas x offset
                               0));               // texture atlas y offset

        coords.push_back(Point(x2 + w,
                               -y2,
                               chars[*p].xOffset + chars[*p].bitmapWidth / _atlasWidth, 
                               0));

        coords.push_back(Point(x2,
                               -y2 - h, 
                               chars[*p].xOffset, 
                               chars[*p].bitmapHeight / _atlasHeight));

        coords.push_back(Point(x2 + w, 
                               -y2,
                               chars[*p].xOffset + chars[*p].bitmapWidth / _atlasWidth, 
                               0));

        coords.push_back(Point(x2,
                               -y2 - h, 
                               chars[*p].xOffset, 
                               chars[*p].bitmapHeight / _atlasHeight));

        coords.push_back(Point(x2 + w, 
                               -y2 - h, 
                               chars[*p].xOffset + chars[*p].bitmapWidth / _atlasWidth, 
                               chars[*p].bitmapHeight / _atlasHeight));
    }

    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Point), 0);
    glBufferData(GL_ARRAY_BUFFER, coords.size() * sizeof(Point), coords.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, coords.size());

    glDisableVertexAttribArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDisable(GL_BLEND);
    glUseProgram(0);
}

void GLFont::loadShader(char* shaderSource, GLenum shaderType) {
    GLuint shaderId = glCreateShader(shaderType);

    GLint result = GL_FALSE; // compilation result
    int infoLogLength; // length of info log

    std::ifstream shaderFile(shaderSource);
    std::string shaderStr;
    const char* shader;

    if(!shaderFile.is_open()) {
        std::string error = "Error: could not read file ";
        throw std::exception(error.append(shaderSource).c_str());
    }
    
    // Read shader
    std::string buffer;
    while(std::getline(shaderFile, buffer)) {
        shaderStr += buffer + "\n";
    }

    shader = shaderStr.c_str();

    // Compile shader
    printf("Compiling shader\n");
    glShaderSource(shaderId,        // Shader handle
                   1,               // Number of files
                   &shader,  // Shader source code
                   NULL);           // NULL terminated string
    glCompileShader(shaderId);

    // Check shader
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &result);
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &infoLogLength);
    std::vector<char> errorMessage(infoLogLength);
    glGetShaderInfoLog(shaderId, infoLogLength, NULL, &errorMessage[0]);
    fprintf(stdout, "%s\n", &errorMessage[0]);

    // Link the program
    fprintf(stdout, "Linking program\n");
    glAttachShader(_programId, shaderId);
    glLinkProgram(_programId);

    // Check the program
    glGetProgramiv(_programId, GL_LINK_STATUS, &result);
    glGetProgramiv(_programId, GL_INFO_LOG_LENGTH, &infoLogLength);
    std::vector<char> programErrorMessage(std::max(infoLogLength, int(1)));
    glGetProgramInfoLog(_programId, infoLogLength, NULL, &programErrorMessage[0]);
    fprintf(stdout, "%s\n", &programErrorMessage[0]);

    glDeleteShader(shaderId);

    shaderFile.close();
}

void GLFont::setColor(float r, float b, float g, float a) {
    _textColor = glm::vec4(r, b, g, a);

    // Update the textColor uniform
    glUseProgram(_programId);
    glUniform4fv(_uniformTextColorHandle, 1, glm::value_ptr(_textColor));
    glUseProgram(0);
}

glm::vec4 GLFont::getColor() {
    return _textColor;
}

void GLFont::setFont(char* font) {
    _font = font;

    // Create a new font
    _error = FT_New_Face(_ft,     // FreeType instance handle
                         _font,   // Font family to use
                         0,       // index of font (in case there are more than one in the file)
                         &_face); // font face handle
    if(_error == FT_Err_Unknown_File_Format) {
        throw std::exception("Failed to open font: unknown font format");
    }
    else if(_error) {
        throw std::exception("Failed to open font");
    }
}

char* GLFont::getFont() {
    return _font;
}

void GLFont::setAlignment(GLFont::FontAlignment alignment) {
    _alignment = alignment;
}

GLFont::FontAlignment GLFont::getAlignment() {
    return _alignment;
}

void GLFont::calculateAlignment(const unsigned char* text, float &x) {
    if(_alignment == GLFont::FontAlignment::LeftAligned)
        return; // no need to calculate alignment

    FT_GlyphSlot slot = _face->glyph;
    const unsigned char* p;
    float totalWidth = 0; // total width of the text to render in window space
   
    // Calculate total width
    for(p = text; *p; ++p) {
        _error = FT_Load_Char(_face, *p, FT_LOAD_RENDER);
        if(_error)
            continue; // skip character

        totalWidth += slot->bitmap.width;
    }

    if(_alignment == GLFont::FontAlignment::CenterAligned)
        x -= totalWidth / 2.0;
    else if(_alignment == GLFont::FontAlignment::RightAligned)
        x -= totalWidth;
}

void GLFont::setPixelSize(int size) {
    if(size > 64) // we do not support > 64 pixels at the moment
        size = 64;

    _pixelSize = size;

    // Set pixel size to 48 x 48 & load character
    _error = FT_Set_Pixel_Sizes(_face,       // font face handle
                                0,           // pixel width  (value of 0 means 'same as the other')
                                _pixelSize); // pixel height (value of 0 means 'same as the other')

    if(_error)
        throw std::exception("Failed to size font (are you using a fixed-size font?)");

    // Here we will load an atlas texture (combined texture of all character glyphs) for the font at this pixel size
    FT_GlyphSlot slot = _face->glyph;
    int width = 0; // width of the texture
    int height = 0; // height of the texture

    // Main char set (32 - 128)
    for(int i = 32; i < 128; ++i) {
        if(FT_Load_Char(_face, i, FT_LOAD_RENDER)) {
            fprintf(stderr, "Loading character %c failed!\n", i);
            continue; // try next character
        }

        width += slot->bitmap.width + 2; // add the width of this glyph to our texture width
        // Note: We add 2 pixels of blank space between glyphs for padding - this helps reduce texture bleeding
        //       that can occur with antialiasing

        height = std::max(height, (int)slot->bitmap.rows);
    }

    _atlasWidth = width;
    _atlasHeight = height;

    glGenTextures(1, &_texIds[_pixelSize]);
    glActiveTexture(GL_TEXTURE0 + _texIds[_pixelSize]);
    glBindTexture(GL_TEXTURE_2D, _texIds[_pixelSize]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Get shader handles
    _uniformTextureHandle = glGetUniformLocation(_programId, "tex");
    _uniformTextColorHandle = glGetUniformLocation(_programId, "textColor");
    _uniformMVPHandle = glGetUniformLocation(_programId, "mvp");

    glUniform1i(_uniformTextureHandle, _texIds[_pixelSize]);
    glUniform4fv(_uniformTextColorHandle, 1, glm::value_ptr(_textColor));
    glUniformMatrix4fv(_uniformMVPHandle, 1, GL_FALSE, glm::value_ptr(_mvp));

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Create an empty texture with the correct dimensions
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);

    int texPos = 0; // texture offset

    for(int i = 32; i < 128; ++i) {
        if(FT_Load_Char(_face, i, FT_LOAD_RENDER))
            continue;

        // Add this character glyph to our texture
        glTexSubImage2D(GL_TEXTURE_2D, 0, texPos, 0, 1, slot->bitmap.rows, GL_RED, GL_UNSIGNED_BYTE, (char)0); // padding
        glTexSubImage2D(GL_TEXTURE_2D, 0, texPos, 0, slot->bitmap.width, slot->bitmap.rows, GL_RED, GL_UNSIGNED_BYTE, slot->bitmap.buffer);
        glTexSubImage2D(GL_TEXTURE_2D, 0, texPos, 0, 1, slot->bitmap.rows, GL_RED, GL_UNSIGNED_BYTE, (char)0); // padding

        // Store glyph info in our char array for this pixel size
        chars[i].advanceX = slot->advance.x >> 6;
        chars[i].advanceY = slot->advance.y >> 6;

        chars[i].bitmapWidth = slot->bitmap.width;
        chars[i].bitmapHeight = slot->bitmap.rows;

        chars[i].bitmapLeft = slot->bitmap_left;
        chars[i].bitmapTop = slot->bitmap_top;

        chars[i].xOffset = (float)texPos / (float)width;
        
        // Increase texture offset
        texPos += slot->bitmap.width + 2;
    }
}

void GLFont::setWindowSize(int width, int height) {
    _windowWidth = width;
    _windowHeight = height;

    // Recalculate sx and sy
    _sx = 2.0 / _windowWidth;
    _sy = 2.0 / _windowHeight;

    //// Recalculate projection matrix
    //_projection = glm::perspective(45.0f, (float)_windowWidth / (float)_windowHeight, 0.1f, 100.0f);
    //recalculateMVP();
}

void GLFont::rotate(float degrees, float x, float y, float z) {
    float rad = degrees * DEG_TO_RAD;
    _model = glm::rotate(_model, rad, glm::vec3(x, y, z));
    recalculateMVP();
}

void GLFont::scale(float x, float y, float z) {
    _model = glm::scale(glm::vec3(x, y, z));
    recalculateMVP();
}

void GLFont::recalculateMVP() {
    _mvp = _projection * _view * _model;

    if(_uniformMVPHandle != -1) {
        glUseProgram(_programId);
        glUniformMatrix4fv(_uniformMVPHandle, 1, GL_FALSE, glm::value_ptr(_mvp));
        glUseProgram(0);
    }
}