
#include "ShaderProgram.h"


#include <algorithm>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

namespace {
// GL type and log label per stage, indexed like ShaderProgram's stage constants.
struct StageInfo {
   unsigned int m_glType;
   const char* m_label;
};
constexpr std::array<StageInfo, 5> s_stageInfo{ {
   { GL_VERTEX_SHADER, "VERTEX" },
   { GL_FRAGMENT_SHADER, "FRAGMENT" },
   { GL_TESS_CONTROL_SHADER, "TESS_CONTR" },
   { GL_TESS_EVALUATION_SHADER, "TESS_EVAL" },
   { GL_GEOMETRY_SHADER, "GEOMETRY" },
} };
}

std::string ShaderProgram::expandIncludes(
   const std::string& path, std::vector<std::string>& includedPaths
) {
   std::error_code error{};
   std::filesystem::path canonical{ std::filesystem::weakly_canonical(path, error) };
   std::string canonicalPath{ error ? path : canonical.string() };
   // Include-once: a file already expanded in this load (including via a cycle)
   // expands to nothing.
   if (std::find(includedPaths.begin(), includedPaths.end(), canonicalPath)
         != includedPaths.end()) {
      return "";
   }
   includedPaths.push_back(canonicalPath);

   std::ifstream ifs(path);
   if (ifs.fail()) {
      std::cout << "ERROR: Could not find file at: " << path << std::endl;
      throw std::runtime_error("ERROR: Could not find file at: " + path);
   }

   std::filesystem::path directory{ std::filesystem::path{ path }.parent_path() };
   std::string result{};
   for (std::string line; std::getline(ifs, line); ) {
      size_t firstChar{ line.find_first_not_of(" \t") };
      if (firstChar != std::string::npos && line.compare(firstChar, 8, "#include") == 0) {
         size_t openQuote{ line.find('"', firstChar + 8) };
         size_t closeQuote{ openQuote == std::string::npos
            ? std::string::npos : line.find('"', openQuote + 1) };
         if (closeQuote == std::string::npos) {
            std::cout << "ERROR: Malformed #include in " << path << ": " << line << std::endl;
            throw std::runtime_error("ERROR: Malformed #include in " + path);
         }
         std::string includePath{ line.substr(openQuote + 1, closeQuote - openQuote - 1) };
         result += expandIncludes((directory / includePath).string(), includedPaths);
         continue;
      }
      result += line;
      result += '\n';
   }
   return result;
}

std::string ShaderProgram::loadTextFileFromPath(std::string path) {
   std::vector<std::string> includedPaths{};
   return expandIncludes(path, includedPaths);
}

std::string ShaderProgram::injectEngineDefines(std::string code) {
   std::string defines{
      "#define MAX_TEXTURE_UNITS " + std::to_string(s_maxTextureUnits) + "\n" };
   size_t versionPos{ code.find("#version") };
   if (versionPos == std::string::npos) {
      return defines + code;
   }
   size_t versionLineEnd{ code.find('\n', versionPos) };
   if (versionLineEnd == std::string::npos) {
      return code + "\n" + defines;
   }
   return code.substr(0, versionLineEnd + 1) + defines + code.substr(versionLineEnd + 1);
}

void ShaderProgram::printWithLineNumbers(std::string code) {
   std::cout << "Code:\n" << std::endl;
   int lineNum{ 1 };
   std::istringstream sStream{ code };
   for (std::string line; std::getline(sStream, line); ) {
      std::string lineNumStr{ std::to_string(lineNum++) };
      std::cout << lineNumStr;
      for (size_t ii = 0; ii < 4 - lineNumStr.size(); ii++) {
         std::cout << " ";
      }
      std::cout << line << std::endl;
   }
   return;
}

void ShaderProgram::loadStage(size_t stageIndex, std::string code) {
   static_assert(s_stageInfo.size() == s_stageCount,
      "stage info table must cover every stage");
   const StageInfo& info{ s_stageInfo[stageIndex] };
   Stage& stage{ m_stages[stageIndex] };

   code = injectEngineDefines(code);
   stage.m_shader = glCreateShader(info.m_glType);
   const char* codePtr{ code.c_str() };
   glShaderSource(stage.m_shader, 1, &codePtr, NULL);
   glCompileShader(stage.m_shader);

   int success{ 0 };
   char infoLog[512];
   glGetShaderiv(stage.m_shader, GL_COMPILE_STATUS, &success);
   if (!success) {
      glGetShaderInfoLog(stage.m_shader, 512, NULL, infoLog);
      printWithLineNumbers(code);
      std::string label{ info.m_label };
      std::cout << std::endl << "ERROR::SHADER::" << label << "::COMPILATION_FAILED\n"
         << infoLog << std::endl;
      throw std::runtime_error("ERROR: SHADER::" + label + "::COMPILATION_FAILED.");
   }
   stage.m_isLoaded = true;
}

void ShaderProgram::loadStageFromPath(size_t stageIndex, std::string path) {
   std::string code{ loadTextFileFromPath(path) };
   m_stages[stageIndex].m_path = path;
   m_hasStoredPaths = true;
   loadStage(stageIndex, code);
}

void ShaderProgram::loadVertexShaderFromPath(std::string vertexCodePath) {
   loadStageFromPath(s_vertexStage, vertexCodePath);
}

void ShaderProgram::loadFragmentShaderFromPath(std::string fragmentCodePath) {
   loadStageFromPath(s_fragmentStage, fragmentCodePath);
}

void ShaderProgram::loadTessellationControlShaderFromPath(std::string tessContrCodePath) {
   loadStageFromPath(s_tessControlStage, tessContrCodePath);
}

void ShaderProgram::loadTessellationEvaluationShaderFromPath(std::string tessEvalCodePath) {
   loadStageFromPath(s_tessEvaluationStage, tessEvalCodePath);
}

void ShaderProgram::loadGeometryShaderFromPath(std::string geometryCodePath) {
   loadStageFromPath(s_geometryStage, geometryCodePath);
}

void ShaderProgram::loadVertexShader(std::string vertexCode) {
   loadStage(s_vertexStage, vertexCode);
}

void ShaderProgram::loadFragmentShader(std::string fragmentCode) {
   loadStage(s_fragmentStage, fragmentCode);
}

void ShaderProgram::loadTessellationControlShader(std::string tessContrCode) {
   loadStage(s_tessControlStage, tessContrCode);
}

void ShaderProgram::loadTessellationEvaluationShader(std::string tessEvalCode) {
   loadStage(s_tessEvaluationStage, tessEvalCode);
}

void ShaderProgram::loadGeometryShader(std::string geometryCode) {
   loadStage(s_geometryStage, geometryCode);
}

void ShaderProgram::linkShaders() {
   if (!m_stages[s_vertexStage].m_isLoaded) {
      std::cout << "ERROR: Cannot link shader program before loading vertex shader." << std::endl;
      throw std::runtime_error("ERROR: Cannot link shader program before loading vertex shader.");
   }
   if (!m_stages[s_fragmentStage].m_isLoaded) {
      std::cout << "ERROR: Cannot link shader program before loading fragment shader." << std::endl;
      throw std::runtime_error("ERROR: Cannot link shader program before loading fragment shader.");
   }
   // link shaders
   m_shaderProgram = glCreateProgram();
   for (const Stage& stage : m_stages) {
      if (stage.m_isLoaded) {
         glAttachShader(m_shaderProgram, stage.m_shader);
      }
   }
   glLinkProgram(m_shaderProgram);
   // check for linking errors
   int success{ 0 };
   char infoLog[512];
   glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &success);
   if (!success) {
      glGetProgramInfoLog(m_shaderProgram, 512, NULL, infoLog);
      std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
      throw std::runtime_error("ERROR: SHADER::PROGRAM::LINKING_FAILED.");
   }
   // The program keeps the linked binaries; the stage shader objects can go.
   for (const Stage& stage : m_stages) {
      if (stage.m_isLoaded) {
         glDeleteShader(stage.m_shader);
      }
   }
   m_programIsLinked = true;
   cacheTextureUnitLocations();
}

void ShaderProgram::cacheTextureUnitLocations() {
   // Once per link rather than once per draw. The names are fixed by the
   // MAX_TEXTURE_UNITS this class injects, and a location cannot change while a
   // program stays linked.
   for (int unit{ 0 }; unit < s_maxTextureUnits; ++unit) {
      const std::string name{ "u_textures[" + std::to_string(unit) + "]" };
      m_textureUnitLocations[unit] = glGetUniformLocation(m_shaderProgram, name.c_str());
   }
}

GLint ShaderProgram::getTextureUnitLocation(int unit) const {
   if (unit < 0 || unit >= s_maxTextureUnits) return -1;
   return m_textureUnitLocations[unit];
}

unsigned int ShaderProgram::getID() {
   if (m_programIsLinked) {
      return m_shaderProgram;
   }
   std::cout << "ERROR: Shader program is not linked." << std::endl;
   throw std::runtime_error("ERROR: Shader program is not linked.");
}

void ShaderProgram::use() {
   if (!m_programIsLinked) {
      std::cout << "ERROR: Cannot use a program before it is linked." << std::endl;
      throw std::runtime_error("ERROR: Cannot use a program before it is linked.");
   }
   glUseProgram(m_shaderProgram);
}

std::pair<bool, std::string> ShaderProgram::reloadShaders() {
   if (!m_hasStoredPaths) {
      return {false, "Cannot reload shaders: no file paths stored (shaders were loaded from strings)"};
   }

   std::string errorMessages{};
   bool hasErrors{ false };

   // Snapshot so a failed reload can restore the working program.
   std::array<Stage, s_stageCount> oldStages{ m_stages };
   unsigned int oldShaderProgram{ m_shaderProgram };
   bool oldProgramIsLinked{ m_programIsLinked };

   // Reset for the new load; stage paths are kept, they are what reload reads.
   for (Stage& stage : m_stages) {
      stage.m_shader = 0;
      stage.m_isLoaded = false;
   }
   m_shaderProgram = 0;
   m_programIsLinked = false;

   // Reload every stage that was loaded from a file. Stages loaded from strings
   // (empty path) cannot be reloaded and stay unloaded, which fails the link
   // below if they were required.
   for (size_t ii = 0; ii < s_stageCount; ii++) {
      if (!oldStages[ii].m_isLoaded || m_stages[ii].m_path.empty()) {
         continue;
      }
      try {
         loadStageFromPath(ii, m_stages[ii].m_path);
      } catch (const std::exception& e) {
         hasErrors = true;
         errorMessages += std::string{ s_stageInfo[ii].m_label }
            + " shader reload failed: " + e.what() + "\n";
      }
   }

   if (m_stages[s_vertexStage].m_isLoaded && m_stages[s_fragmentStage].m_isLoaded) {
      try {
         linkShaders();
      } catch (const std::exception& e) {
         hasErrors = true;
         errorMessages += "Shader linking failed: " + std::string{ e.what() } + "\n";
      }
   }

   if (hasErrors || !m_programIsLinked) {
      // Clean up whatever the failed reload created, then restore the old program.
      for (const Stage& stage : m_stages) {
         if (stage.m_shader != 0) {
            glDeleteShader(stage.m_shader);
         }
      }
      if (m_shaderProgram != 0) {
         glDeleteProgram(m_shaderProgram);
      }
      m_stages = oldStages;
      m_shaderProgram = oldShaderProgram;
      m_programIsLinked = oldProgramIsLinked;
      return {false, errorMessages.empty() ? "Shader reload failed" : errorMessages};
   }

   // Success: the old program (and its attached shaders) can go.
   if (oldShaderProgram != 0) {
      glDeleteProgram(oldShaderProgram);
   }
   return {true, "Shaders reloaded successfully"};
}

// Uniform setters
void ShaderProgram::setUniformMatrix4f(const std::string& name, const glm::mat4& matrix) {
    GLint location = glGetUniformLocation(m_shaderProgram, name.c_str());
    if (location != -1) {
        glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }
}

void ShaderProgram::setUniformVec3(const std::string& name, const glm::dvec3& value) {
    GLint location = glGetUniformLocation(m_shaderProgram, name.c_str());
    if (location != -1) {
        // Convert double precision to single precision
        glm::vec3 floatValue(value);
        glUniform3fv(location, 1, glm::value_ptr(floatValue));
    }
}

void ShaderProgram::setUniformVec4(const std::string& name, const glm::dvec4& value) {
    GLint location = glGetUniformLocation(m_shaderProgram, name.c_str());
    if (location != -1) {
        // Convert double precision to single precision
        glm::vec4 floatValue(value);
        glUniform4fv(location, 1, glm::value_ptr(floatValue));
    }
}

void ShaderProgram::setUniformFloat(const std::string& name, float value) {
    GLint location = glGetUniformLocation(m_shaderProgram, name.c_str());
    if (location != -1) {
        glUniform1f(location, value);
    }
}

void ShaderProgram::setUniformInt(const std::string& name, int value) {
    GLint location = glGetUniformLocation(m_shaderProgram, name.c_str());
    if (location != -1) {
        glUniform1i(location, value);
    }
}

void ShaderProgram::setUniformUInt32(const std::string& name, unsigned int value) {
    GLint location = glGetUniformLocation(m_shaderProgram, name.c_str());
    if (location != -1) {
        glUniform1ui(location, value);
    }
}

void ShaderProgram::setUniformBool(const std::string& name, bool value) {
    GLint location = glGetUniformLocation(m_shaderProgram, name.c_str());
    if (location != -1) {
        glUniform1i(location, static_cast<int>(value));
    }
}


ShaderProgram::ShaderProgram() {
}

ShaderProgram::~ShaderProgram() {
   if (m_programIsLinked) {
      glDeleteProgram(m_shaderProgram);
   } else {
      for (const Stage& stage : m_stages) {
         if (stage.m_shader != 0) {
            // 0 means it is not created yet.
            glDeleteShader(stage.m_shader);
         }
      }
   }
}
