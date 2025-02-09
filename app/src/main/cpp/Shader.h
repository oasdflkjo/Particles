#ifndef ANDROIDGLINVESTIGATIONS_SHADER_H
#define ANDROIDGLINVESTIGATIONS_SHADER_H

#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <memory>
#include <vector>
#include <string>
#include "AndroidOut.h"

class Shader {
public:
    static GLuint loadShader(GLenum shaderType, const std::string& shaderSource);

    static Shader* loadShader(
            const std::string& vertexSource,
            const std::string& fragmentSource,
            const std::string& positionAttributeName,
            const std::string& uvAttributeName,
            const std::string& projectionMatrixUniformName);

    static Shader* loadComputeShader(const std::string& computeSource);

    Shader(GLuint program, GLint position, GLint uv, GLint projectionMatrix) :
        program_(program),
        projectionMatrix_(projectionMatrix) {
        if (program_ == 0) {
            aout << "Invalid shader program" << std::endl;
            return;
        }
        aout << "Created shader with program=" << program 
             << " proj=" << projectionMatrix << std::endl;
    }

    explicit Shader(GLuint program) :
        program_(program),
        projectionMatrix_(-1) {
        aout << "Created compute shader with program=" << program << std::endl;
    }

    ~Shader() {
        if (program_) {
            glDeleteProgram(program_);
            program_ = 0;
        }
    }

    bool activate() const;
    void deactivate() const;
    bool setProjectionMatrix(float* projectionMatrix) const;
    GLuint program() const { return program_; }
    bool checkError(const char* operation) const;

private:
    static GLuint compileShader(GLenum type, const std::string &source);
    static GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);

    GLuint program_;
    GLint projectionMatrix_;
};

#endif //ANDROIDGLINVESTIGATIONS_SHADER_H