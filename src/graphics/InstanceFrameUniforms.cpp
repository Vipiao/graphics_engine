// InstanceFrameUniforms.cpp
#include "InstanceFrameUniforms.h"
#include "math/DekkerArithmetic.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

void setInstanceFrameUniforms(unsigned int programID, const FrameRenderParams& params) {
    const glm::mat4 viewFloat{ params.view };
    const glm::mat4 projectionFloat{ params.projection };

    GLint loc;
    if ((loc = glGetUniformLocation(programID, "view")) != -1)
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(viewFloat));
    if ((loc = glGetUniformLocation(programID, "projection")) != -1)
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(projectionFloat));
    if ((loc = glGetUniformLocation(programID, "u_frame")) != -1)
        glUniform1ui(loc, static_cast<GLuint>(params.frame));
    if ((loc = glGetUniformLocation(programID, "u_time")) != -1)
        glUniform1ui(loc, static_cast<GLuint>(params.time));
    if ((loc = glGetUniformLocation(programID, "u_timeRemainder")) != -1)
        glUniform1f(loc, static_cast<float>(params.timeRemainder));

    // Camera position split into high/low floats (Dekker) so camera-relative
    // vertex math keeps precision far from the origin. The DekkerNumber(double)
    // constructor captures the low-order bits; passing a float would drop them.
    typedef DekkerArithmetic<float> DekkerFloat;
    const DekkerFloat::DekkerNumber camX(params.camPos.x);
    const DekkerFloat::DekkerNumber camY(params.camPos.y);
    const DekkerFloat::DekkerNumber camZ(params.camPos.z);
    const glm::vec3 camPosHigh(camX.main, camY.main, camZ.main);
    const glm::vec3 camPosLow(camX.error, camY.error, camZ.error);
    if ((loc = glGetUniformLocation(programID, "u_cameraPositionHigh")) != -1)
        glUniform3fv(loc, 1, glm::value_ptr(camPosHigh));
    if ((loc = glGetUniformLocation(programID, "u_cameraPositionLow")) != -1)
        glUniform3fv(loc, 1, glm::value_ptr(camPosLow));
}
