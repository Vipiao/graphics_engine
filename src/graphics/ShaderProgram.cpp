
#include "ShaderProgram.h"


#include <iostream>
#include <fstream>
#include <sstream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

std::string ShaderProgram::loadTextFileFromPath(std::string path) {
   std::ifstream ifs(path);
   if (ifs.fail()) {
      std::cout << "ERROR: Could not find file at: " << path << std::endl;
      throw std::runtime_error("ERROR: Could not find file at: " + path);
   }
   std::string text((std::istreambuf_iterator<char>(ifs)),
      (std::istreambuf_iterator<char>()));
   return text;
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

void ShaderProgram::loadVertexShaderFromPath(std::string vertexCodePath) {
   std::string vertexCode(ShaderProgram::loadTextFileFromPath(vertexCodePath));
   m_vertexShaderPath = vertexCodePath;
   m_hasStoredPaths = true;
   loadVertexShader(vertexCode);
}

void ShaderProgram::loadFragmentShaderFromPath(std::string fragmentCodePath) {
   std::string fragmentCode(ShaderProgram::loadTextFileFromPath(fragmentCodePath));
   m_fragmentShaderPath = fragmentCodePath;
   m_hasStoredPaths = true;
   loadFragmentShader(fragmentCode);
}

void ShaderProgram::loadTessellationControlShaderFromPath(std::string tessContrCodePath) {
   std::string tessCode(ShaderProgram::loadTextFileFromPath(tessContrCodePath));
   m_tessellationControlShaderPath = tessContrCodePath;
   m_hasStoredPaths = true;
   loadTessellationControlShader(tessCode);
}

void ShaderProgram::loadTessellationEvaluationShaderFromPath(std::string tessEvalCodePath) {
   std::string tessCode(ShaderProgram::loadTextFileFromPath(tessEvalCodePath));
   m_tessellationEvaluationShaderPath = tessEvalCodePath;
   m_hasStoredPaths = true;
   loadTessellationEvaluationShader(tessCode);
}

void ShaderProgram::loadGeometryShaderFromPath(std::string geometryCodePath) {
   std::string geometryCode(ShaderProgram::loadTextFileFromPath(geometryCodePath));
   m_geometryShaderPath = geometryCodePath;
   m_hasStoredPaths = true;
   loadGeometryShader(geometryCode);
}

void ShaderProgram::loadVertexShader(std::string vertexCode) {
   // vertex shader
   m_vertexShader = glCreateShader(GL_VERTEX_SHADER);
   const char* vertCode = vertexCode.c_str();
   glShaderSource(m_vertexShader, 1, &vertCode, NULL);
   glCompileShader(m_vertexShader);
   // Check for shader compile errors.
   int success;
   char infoLog[512];
   glGetShaderiv(m_vertexShader, GL_COMPILE_STATUS, &success);
   if (!success) {
      glGetShaderInfoLog(m_vertexShader, 512, NULL, infoLog);
      ShaderProgram::printWithLineNumbers(vertCode);
      std::cout << std::endl << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
      throw std::runtime_error("ERROR: SHADER::VERTEX::COMPILATION_FAILED.");
   }
   m_vertexShaderIsLoaded = true;
}

void ShaderProgram::loadFragmentShader(std::string fragmentCode) {
   // fragment shader
   m_fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
   const char* fragCode = fragmentCode.c_str();
   glShaderSource(m_fragmentShader, 1, &fragCode, NULL);
   glCompileShader(m_fragmentShader);
   // Check for shader compile errors.
   int success;
   char infoLog[512];
   glGetShaderiv(m_fragmentShader, GL_COMPILE_STATUS, &success);
   if (!success) {
      glGetShaderInfoLog(m_fragmentShader, 512, NULL, infoLog);
      ShaderProgram::printWithLineNumbers(fragmentCode);
      std::cout << std::endl << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
      throw std::runtime_error("ERROR: SHADER::FRAGMENT::COMPILATION_FAILED.");
   }
   m_fragmentShaderIsLoaded = true;
}

void ShaderProgram::loadTessellationControlShader(std::string tessContrCode) {
   // Tesselation control shader.
   m_tessellationControlShader = glCreateShader(GL_TESS_CONTROL_SHADER);
   const char* tessCode = tessContrCode.c_str();
   glShaderSource(m_tessellationControlShader, 1, &tessCode, NULL);
   glCompileShader(m_tessellationControlShader);
   // Check for shader compile errors.
   int success;
   char infoLog[512];
   glGetShaderiv(m_tessellationControlShader, GL_COMPILE_STATUS, &success);
   if (!success) {
      glGetShaderInfoLog(m_tessellationControlShader, 512, NULL, infoLog);
      ShaderProgram::printWithLineNumbers(tessCode);
      std::cout << std::endl << "ERROR::SHADER::TESS_CONTR::COMPILATION_FAILED\n" << infoLog << std::endl;
      throw std::runtime_error("ERROR: SHADER::TESS_CONTR::COMPILATION_FAILED.");
   }
   m_tessellationControlShaderIsLoaded = true;
}

void ShaderProgram::loadTessellationEvaluationShader(std::string tessEvalCode) {
   // Tesselation evaluation shader.
   m_tessellationEvaluationShader = glCreateShader(GL_TESS_EVALUATION_SHADER);
   const char* tessCode = tessEvalCode.c_str();
   glShaderSource(m_tessellationEvaluationShader, 1, &tessCode, NULL);
   glCompileShader(m_tessellationEvaluationShader);
   // Check for shader compile errors.
   int success;
   char infoLog[512];
   glGetShaderiv(m_tessellationEvaluationShader, GL_COMPILE_STATUS, &success);
   if (!success) {
      glGetShaderInfoLog(m_tessellationEvaluationShader, 512, NULL, infoLog);
      ShaderProgram::printWithLineNumbers(tessCode);
      std::cout << std::endl << "ERROR::SHADER::TESS_EVAL::COMPILATION_FAILED\n" << infoLog << std::endl;
      throw std::runtime_error("ERROR: SHADER::TESS_EVAL::COMPILATION_FAILED.");
   }
   m_tessellationEvaluationShaderIsLoaded = true;
}

void ShaderProgram::loadGeometryShader(std::string geometryCode) {
   // Geometry shader.
   m_geometryShader = glCreateShader(GL_GEOMETRY_SHADER);
   const char* geomCode = geometryCode.c_str();
   glShaderSource(m_geometryShader, 1, &geomCode, NULL);
   glCompileShader(m_geometryShader);
   // Check for shader compile errors.
   int success;
   char infoLog[512];
   glGetShaderiv(m_geometryShader, GL_COMPILE_STATUS, &success);
   if (!success) {
      glGetShaderInfoLog(m_geometryShader, 512, NULL, infoLog);
      ShaderProgram::printWithLineNumbers(geomCode);
      std::cout << std::endl << "ERROR::SHADER::GEOMETRY::COMPILATION_FAILED\n" << infoLog << std::endl;
      throw std::runtime_error("ERROR: SHADER::GEOMETRY::COMPILATION_FAILED.");
   }
   m_geometryShaderIsLoaded = true;
}

void ShaderProgram::linkShaders() {
   if (!m_vertexShaderIsLoaded) {
      std::cout << "ERROR: Cannot link shader program before loading vertex shader." << std::endl;
      throw std::runtime_error("ERROR: Cannot link shader program before loading vertex shader.");
   }
   if (!m_fragmentShaderIsLoaded) {
      std::cout << "ERROR: Cannot link shader program before loading fragment shader." << std::endl;
      throw std::runtime_error("ERROR: Cannot link shader program before loading fragment shader.");
   }
   //if (!m_tesselationControlShaderIsLoaded) {
   //   throw std::runtime_error (
   //      "ERROR: Cannot link shader program before loading tesselation shader."
   //   );
   //}
   //if (!m_tesselationEvaluationShaderIsLoaded) {
   //   throw std::runtime_error (
   //      "ERROR: Cannot link shader program before loading tesselation shader."
   //   );
   //}
   // link shaders
   m_shaderProgram = glCreateProgram();
   glAttachShader(m_shaderProgram, m_vertexShader);
   glAttachShader(m_shaderProgram, m_fragmentShader);
   if (m_tessellationControlShaderIsLoaded) {
      glAttachShader(m_shaderProgram, m_tessellationControlShader);
   }
   if (m_tessellationEvaluationShaderIsLoaded) {
      glAttachShader(m_shaderProgram, m_tessellationEvaluationShader);
   }
   if (m_geometryShaderIsLoaded) {
      glAttachShader(m_shaderProgram, m_geometryShader);
   }
   glLinkProgram(m_shaderProgram);
   // check for linking errors
   int success;
   char infoLog[512];
   glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &success);
   if (!success) {
      glGetProgramInfoLog(m_shaderProgram, 512, NULL, infoLog);
      std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
      throw std::runtime_error("ERROR: SHADER::PROGRAM::LINKING_FAILED.");
   }
   glDeleteShader(m_vertexShader);
   glDeleteShader(m_fragmentShader);
   if (m_tessellationControlShaderIsLoaded) {
      glDeleteShader(m_tessellationControlShader);
   }
   if (m_tessellationEvaluationShaderIsLoaded) {
      glDeleteShader(m_tessellationEvaluationShader);
   }
   if (m_geometryShaderIsLoaded) {
      glDeleteShader(m_geometryShader);
   }
   m_programIsLinked = true;
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
   
   std::string errorMessages;
   bool hasErrors = false;
   
   // Store old shader IDs
   unsigned int oldVertexShader = m_vertexShader;
   unsigned int oldFragmentShader = m_fragmentShader;
   unsigned int oldTessellationControlShader = m_tessellationControlShader;
   unsigned int oldTessellationEvaluationShader = m_tessellationEvaluationShader;
   unsigned int oldGeometryShader = m_geometryShader;
   unsigned int oldShaderProgram = m_shaderProgram;
   bool oldProgramIsLinked = m_programIsLinked;
   
   // Store old load states
   bool oldVertexShaderIsLoaded = m_vertexShaderIsLoaded;
   bool oldFragmentShaderIsLoaded = m_fragmentShaderIsLoaded;
   bool oldTessellationControlShaderIsLoaded = m_tessellationControlShaderIsLoaded;
   bool oldTessellationEvaluationShaderIsLoaded = m_tessellationEvaluationShaderIsLoaded;
   bool oldGeometryShaderIsLoaded = m_geometryShaderIsLoaded;
   
   // Reset states for new loading
   m_vertexShader = 0;
   m_fragmentShader = 0;
   m_tessellationControlShader = 0;
   m_tessellationEvaluationShader = 0;
   m_geometryShader = 0;
   m_shaderProgram = 0;
   m_programIsLinked = false;
   m_vertexShaderIsLoaded = false;
   m_fragmentShaderIsLoaded = false;
   m_tessellationControlShaderIsLoaded = false;
   m_tessellationEvaluationShaderIsLoaded = false;
   m_geometryShaderIsLoaded = false;
   
   try {
      // Try to reload vertex shader if it was loaded
      if (oldVertexShaderIsLoaded && !m_vertexShaderPath.empty()) {
         try {
            std::string vertexCode(ShaderProgram::loadTextFileFromPath(m_vertexShaderPath));
            loadVertexShader(vertexCode);
         } catch (const std::exception& e) {
            hasErrors = true;
            errorMessages += "Vertex shader reload failed: " + std::string(e.what()) + "\n";
         }
      }
      
      // Try to reload fragment shader if it was loaded
      if (oldFragmentShaderIsLoaded && !m_fragmentShaderPath.empty()) {
         try {
            std::string fragmentCode(ShaderProgram::loadTextFileFromPath(m_fragmentShaderPath));
            loadFragmentShader(fragmentCode);
         } catch (const std::exception& e) {
            hasErrors = true;
            errorMessages += "Fragment shader reload failed: " + std::string(e.what()) + "\n";
         }
      }
      
      // Try to reload tessellation control shader if it was loaded
      if (oldTessellationControlShaderIsLoaded && !m_tessellationControlShaderPath.empty()) {
         try {
            std::string tessCode(ShaderProgram::loadTextFileFromPath(m_tessellationControlShaderPath));
            loadTessellationControlShader(tessCode);
         } catch (const std::exception& e) {
            hasErrors = true;
            errorMessages += "Tessellation control shader reload failed: " + std::string(e.what()) + "\n";
         }
      }
      
      // Try to reload tessellation evaluation shader if it was loaded
      if (oldTessellationEvaluationShaderIsLoaded && !m_tessellationEvaluationShaderPath.empty()) {
         try {
            std::string tessCode(ShaderProgram::loadTextFileFromPath(m_tessellationEvaluationShaderPath));
            loadTessellationEvaluationShader(tessCode);
         } catch (const std::exception& e) {
            hasErrors = true;
            errorMessages += "Tessellation evaluation shader reload failed: " + std::string(e.what()) + "\n";
         }
      }
      
      // Try to reload geometry shader if it was loaded
      if (oldGeometryShaderIsLoaded && !m_geometryShaderPath.empty()) {
         try {
            std::string geometryCode(ShaderProgram::loadTextFileFromPath(m_geometryShaderPath));
            loadGeometryShader(geometryCode);
         } catch (const std::exception& e) {
            hasErrors = true;
            errorMessages += "Geometry shader reload failed: " + std::string(e.what()) + "\n";
         }
      }
      
      // Try to link the new shaders
      if (m_vertexShaderIsLoaded && m_fragmentShaderIsLoaded) {
         try {
            linkShaders();
         } catch (const std::exception& e) {
            hasErrors = true;
            errorMessages += "Shader linking failed: " + std::string(e.what()) + "\n";
         }
      }
      
   } catch (...) {
      hasErrors = true;
      errorMessages += "Unexpected error during shader reload\n";
   }
   
   // If there were errors, restore old shaders and clean up failed new ones
   if (hasErrors || !m_programIsLinked) {
      // Clean up any partially created new shaders
      if (m_vertexShader != 0) glDeleteShader(m_vertexShader);
      if (m_fragmentShader != 0) glDeleteShader(m_fragmentShader);
      if (m_tessellationControlShader != 0) glDeleteShader(m_tessellationControlShader);
      if (m_tessellationEvaluationShader != 0) glDeleteShader(m_tessellationEvaluationShader);
      if (m_geometryShader != 0) glDeleteShader(m_geometryShader);
      if (m_shaderProgram != 0) glDeleteProgram(m_shaderProgram);
      
      // Restore old shaders
      m_vertexShader = oldVertexShader;
      m_fragmentShader = oldFragmentShader;
      m_tessellationControlShader = oldTessellationControlShader;
      m_tessellationEvaluationShader = oldTessellationEvaluationShader;
      m_geometryShader = oldGeometryShader;
      m_shaderProgram = oldShaderProgram;
      m_programIsLinked = oldProgramIsLinked;
      m_vertexShaderIsLoaded = oldVertexShaderIsLoaded;
      m_fragmentShaderIsLoaded = oldFragmentShaderIsLoaded;
      m_tessellationControlShaderIsLoaded = oldTessellationControlShaderIsLoaded;
      m_tessellationEvaluationShaderIsLoaded = oldTessellationEvaluationShaderIsLoaded;
      m_geometryShaderIsLoaded = oldGeometryShaderIsLoaded;
      
      return {false, errorMessages.empty() ? "Shader reload failed" : errorMessages};
   } else {
      // Success! Clean up old shaders
      if (oldShaderProgram != 0) glDeleteProgram(oldShaderProgram);
      // Note: individual shaders are cleaned up by linkShaders()
      
      return {true, "Shaders reloaded successfully"};
   }
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
   //std::cout << std::endl << "++ Shader program" << std::endl << std::endl;
}

ShaderProgram::~ShaderProgram() {
   //std::cout << std::endl << "-- Shader program" << std::endl << std::endl;
   if (m_programIsLinked) {
      glDeleteProgram(m_shaderProgram);
   } else {
      if (m_vertexShader != 0) {
         // 0 means it is not created yet.
         glDeleteShader(m_vertexShader);
      }
      if (m_fragmentShader != 0) {
         // 0 means it is not created yet.
         glDeleteShader(m_fragmentShader);
      }
      if (m_tessellationControlShader != 0) {
         // 0 means it is not created yet.
         glDeleteShader(m_tessellationControlShader);
      }
      if (m_tessellationEvaluationShader != 0) {
         // 0 means it is not created yet.
         glDeleteShader(m_tessellationEvaluationShader);
      }
      if (m_geometryShader != 0) {
         // 0 means it is not created yet.
         glDeleteShader(m_geometryShader);
      }
   }
}
