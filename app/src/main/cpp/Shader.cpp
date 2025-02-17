#include "Shader.h"

#include "AndroidOut.h"
#include "Model.h"
#include "Utility.h"
#include <GLES3/gl31.h>

Shader *Shader::loadShader(
        const std::string &vertexSource,
        const std::string &fragmentSource,
        const std::string &positionAttributeName,
        const std::string &uvAttributeName,
        const std::string &projectionMatrixUniformName) {
    // If no fragment source is provided, treat it as a compute shader
    if (fragmentSource.empty()) {
        return loadComputeShader(vertexSource);
    }

    aout << "Loading shader..." << std::endl;
    aout << "Vertex shader source:\n" << vertexSource << std::endl;
    aout << "Fragment shader source:\n" << fragmentSource << std::endl;

    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertexShader) {
        aout << "Failed to compile vertex shader" << std::endl;
        return nullptr;
    }

    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragmentShader) {
        aout << "Failed to compile fragment shader" << std::endl;
        glDeleteShader(vertexShader);
        return nullptr;
    }

    GLuint program = linkProgram(vertexShader, fragmentShader);
    if (!program) {
        aout << "Failed to link program" << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return nullptr;
    }

    // Get attribute and uniform locations
    GLint positionAttribute = -1;
    GLint uvAttribute = -1;
    GLint projectionMatrixUniform = -1;

    if (!positionAttributeName.empty()) {
        positionAttribute = glGetAttribLocation(program, positionAttributeName.c_str());
        aout << "Position attribute '" << positionAttributeName << "' location: " << positionAttribute << std::endl;
        if (positionAttribute == -1) {
            aout << "Warning: Position attribute not found" << std::endl;
        }
    }

    if (!uvAttributeName.empty()) {
        uvAttribute = glGetAttribLocation(program, uvAttributeName.c_str());
        aout << "UV attribute '" << uvAttributeName << "' location: " << uvAttribute << std::endl;
        if (uvAttribute == -1) {
            aout << "Warning: UV attribute not found" << std::endl;
        }
    }

    if (!projectionMatrixUniformName.empty()) {
        projectionMatrixUniform = glGetUniformLocation(program, projectionMatrixUniformName.c_str());
        aout << "Projection matrix uniform '" << projectionMatrixUniformName << "' location: " << projectionMatrixUniform << std::endl;
        if (projectionMatrixUniform == -1) {
            aout << "Warning: Projection matrix uniform not found" << std::endl;
        }
    }

    // Print all active attributes and uniforms
    GLint numAttributes = 0;
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &numAttributes);
    aout << "Number of active attributes: " << numAttributes << std::endl;
    
    for (GLint i = 0; i < numAttributes; i++) {
        char name[128];
        GLint size;
        GLenum type;
        glGetActiveAttrib(program, i, sizeof(name), nullptr, &size, &type, name);
        aout << "Attribute " << i << ": " << name << " (location: " 
             << glGetAttribLocation(program, name) << ")" << std::endl;
    }
    
    GLint numUniforms = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numUniforms);
    aout << "Number of active uniforms: " << numUniforms << std::endl;

    for (GLint i = 0; i < numUniforms; i++) {
        char name[128];
        GLint size;
        GLenum type;
        glGetActiveUniform(program, i, sizeof(name), nullptr, &size, &type, name);
        aout << "Uniform " << i << ": " << name << " (location: " 
             << glGetUniformLocation(program, name) << ")" << std::endl;
    }

    // Clean up shaders
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return new Shader(program, positionAttribute, uvAttribute, projectionMatrixUniform);
}

GLuint Shader::loadShader(GLenum shaderType, const std::string &shaderSource) {
    Utility::assertGlError();
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        auto *shaderRawString = (GLchar *) shaderSource.c_str();
        GLint shaderLength = shaderSource.length();
        glShaderSource(shader, 1, &shaderRawString, &shaderLength);
        glCompileShader(shader);

        GLint shaderCompiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &shaderCompiled);

        // If the shader doesn't compile, log the result to the terminal for debugging
        if (!shaderCompiled) {
            GLint infoLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLength);

            if (infoLength) {
                auto *infoLog = new GLchar[infoLength];
                glGetShaderInfoLog(shader, infoLength, nullptr, infoLog);
                aout << "Failed to compile with:\n" << infoLog << std::endl;
                delete[] infoLog;
            }

            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

void Shader::activate() const {
    if (!program_ || program_ == 0) {
        throw std::runtime_error("Trying to activate invalid shader program");
    }
    
    GLenum error = glGetError(); // Clear any previous errors
    glUseProgram(program_);
    error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "Error activating shader program " << program_ << ": 0x" << std::hex << error << std::endl;
        throw std::runtime_error("Failed to activate shader program");
    }
    aout << "Activated shader program " << program_ << std::endl;
}

void Shader::deactivate() const {
    glUseProgram(0);
}

void Shader::drawModel(const Model &model) const {
    // The position attribute is 3 floats
    glVertexAttribPointer(
            position_, // attrib
            3, // elements
            GL_FLOAT, // of type float
            GL_FALSE, // don't normalize
            sizeof(Vertex), // stride is Vertex bytes
            model.getVertexData() // pull from the start of the vertex data
    );
    glEnableVertexAttribArray(position_);

    // The uv attribute is 2 floats
    glVertexAttribPointer(
            uv_, // attrib
            2, // elements
            GL_FLOAT, // of type float
            GL_FALSE, // don't normalize
            sizeof(Vertex), // stride is Vertex bytes
            ((uint8_t *) model.getVertexData()) + sizeof(Vector3) // offset Vector3 from the start
    );
    glEnableVertexAttribArray(uv_);

    // Setup the texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, model.getTexture().getTextureID());

    // Draw as indexed triangles
    glDrawElements(GL_TRIANGLES, model.getIndexCount(), GL_UNSIGNED_SHORT, model.getIndexData());

    glDisableVertexAttribArray(uv_);
    glDisableVertexAttribArray(position_);
}

void Shader::setProjectionMatrix(float* projectionMatrix) const {
    if (!program_) {
        throw std::runtime_error("Invalid shader program");
    }
    
    if (projectionMatrix_ == -1) {
        aout << "Warning: Projection matrix uniform location is -1" << std::endl;
        return;
    }
    
    GLenum error = glGetError(); // Clear any previous errors
    glUniformMatrix4fv(projectionMatrix_, 1, false, projectionMatrix);
    error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "Error setting projection matrix: 0x" << std::hex << error << std::endl;
        throw std::runtime_error("Failed to set projection matrix");
    }
}

Shader* Shader::loadComputeShader(const std::string& computeSource) {
    aout << "Creating compute shader..." << std::endl;
    
    GLuint computeShader = loadShader(GL_COMPUTE_SHADER, computeSource);
    if (!computeShader) {
        aout << "Failed to create compute shader" << std::endl;
        return nullptr;
    }

    GLuint program = glCreateProgram();
    if (!program) {
        aout << "Failed to create program" << std::endl;
        glDeleteShader(computeShader);
        return nullptr;
    }

    glAttachShader(program, computeShader);
    glLinkProgram(program);

    // Get link status and log
    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 0) {
            std::vector<char> log(logLength);
            glGetProgramInfoLog(program, logLength, nullptr, log.data());
            aout << "Compute shader link error: " << log.data() << std::endl;
        }
        glDeleteProgram(program);
        glDeleteShader(computeShader);
        return nullptr;
    }

    // Print active uniforms for debugging
    GLint numUniforms = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numUniforms);
    aout << "Number of active uniforms: " << numUniforms << std::endl;
    
    for (GLint i = 0; i < numUniforms; i++) {
        char name[128];
        GLint size;
        GLenum type;
        glGetActiveUniform(program, i, sizeof(name), nullptr, &size, &type, name);
        aout << "Uniform " << i << ": " << name << " (location: " 
             << glGetUniformLocation(program, name) << ")" << std::endl;
    }

    glDeleteShader(computeShader);
    return new Shader(program);
}

void Shader::checkError(const char* operation) const {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "OpenGL error after " << operation << ": 0x" << std::hex << error << std::endl;
        throw std::runtime_error("OpenGL error");
    }
}

GLuint Shader::compileShader(GLenum type, const std::string &source) {
    GLuint shader = glCreateShader(type);
    const char *sourceCStr = source.c_str();
    glShaderSource(shader, 1, &sourceCStr, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        aout << "Shader compilation failed: " << infoLog << std::endl;
        aout << "Shader source: " << std::endl << source << std::endl;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint Shader::linkProgram(GLuint vertexShader, GLuint fragmentShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        aout << "Program linking failed: " << infoLog << std::endl;
        glDeleteProgram(program);
        return 0;
    }

    return program;
}