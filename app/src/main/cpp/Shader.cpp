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

    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertexShader) {
        aout << "Failed to load vertex shader" << std::endl;
        return nullptr;
    }

    GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragmentShader) {
        aout << "Failed to load fragment shader" << std::endl;
        glDeleteShader(vertexShader);
        return nullptr;
    }

    GLuint program = glCreateProgram();
    if (!program) {
        aout << "Failed to create program" << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return nullptr;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 0) {
            std::vector<char> log(logLength);
            glGetProgramInfoLog(program, logLength, nullptr, log.data());
            aout << "Failed to link program: " << log.data() << std::endl;
        }
        glDeleteProgram(program);
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
        aout << "Position attribute location: " << positionAttribute << std::endl;
        if (positionAttribute == -1) {
            aout << "Warning: Position attribute not found" << std::endl;
        }
    }

    if (!uvAttributeName.empty()) {
        uvAttribute = glGetAttribLocation(program, uvAttributeName.c_str());
        aout << "UV attribute location: " << uvAttribute << std::endl;
        if (uvAttribute == -1) {
            aout << "Warning: UV attribute not found" << std::endl;
        }
    }

    if (!projectionMatrixUniformName.empty()) {
        projectionMatrixUniform = glGetUniformLocation(program, projectionMatrixUniformName.c_str());
        aout << "Projection matrix uniform location: " << projectionMatrixUniform << std::endl;
        if (projectionMatrixUniform == -1) {
            aout << "Warning: Projection matrix uniform not found" << std::endl;
        }
    }

    // Print shader program info
    GLint numAttributes = 0;
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &numAttributes);
    aout << "Number of active attributes: " << numAttributes << std::endl;
    
    GLint numUniforms = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numUniforms);
    aout << "Number of active uniforms: " << numUniforms << std::endl;

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

    // Validate program
    glValidateProgram(program);
    GLint validateStatus = GL_FALSE;
    glGetProgramiv(program, GL_VALIDATE_STATUS, &validateStatus);
    if (validateStatus != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 0) {
            std::vector<char> log(logLength);
            glGetProgramInfoLog(program, logLength, nullptr, log.data());
            aout << "Compute shader validation error: " << log.data() << std::endl;
        }
        glDeleteProgram(program);
        glDeleteShader(computeShader);
        return nullptr;
    }

    glDeleteShader(computeShader);
    aout << "Compute shader created successfully" << std::endl;
    return new Shader(program);
}

void Shader::checkError(const char* operation) const {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        aout << "OpenGL error after " << operation << ": 0x" << std::hex << error << std::endl;
        throw std::runtime_error("OpenGL error");
    }
}