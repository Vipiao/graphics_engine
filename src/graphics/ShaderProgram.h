#pragma once

#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <utility>

class ShaderProgram {
private:
   unsigned int m_vertexShader{ 0 };
   unsigned int m_fragmentShader{ 0 };
   unsigned int m_tessellationControlShader{ 0 };
   unsigned int m_tessellationEvaluationShader{ 0 };
   unsigned int m_geometryShader{ 0 };
   unsigned int m_shaderProgram{ 0 };

   bool m_vertexShaderIsLoaded = false;
   bool m_fragmentShaderIsLoaded = false;
   bool m_tessellationControlShaderIsLoaded = false;
   bool m_tessellationEvaluationShaderIsLoaded = false;
   bool m_geometryShaderIsLoaded = false;
   bool m_programIsLinked = false;

   // Shader file paths for reloading
   std::string m_vertexShaderPath;
   std::string m_fragmentShaderPath;
   std::string m_tessellationControlShaderPath;
   std::string m_tessellationEvaluationShaderPath;
   std::string m_geometryShaderPath;
   bool m_hasStoredPaths = false;

public:
   static std::string loadTextFileFromPath(std::string path);
   static void printWithLineNumbers(std::string code);
   void loadVertexShaderFromPath(std::string vertexCodePath);
   void loadFragmentShaderFromPath(std::string fragmentCodePath);
   void loadTessellationControlShaderFromPath(std::string tessContrCodePath);
   void loadTessellationEvaluationShaderFromPath(std::string tessEvalCodePath);
   void loadGeometryShaderFromPath(std::string geometryCodePath);
   void loadVertexShader(std::string vertexCode);
   void loadFragmentShader(std::string fragmentCode);
   void loadTessellationControlShader(std::string tessContrCode);
   void loadTessellationEvaluationShader(std::string tessEvalCode);
   void loadGeometryShader(std::string geometryCode);
   void linkShaders();
   unsigned int getID();
   void use();

   // Shader reloading
   std::pair<bool, std::string> reloadShaders();
   
   // Uniform setters
   void setUniformMatrix4f(const std::string& name, const glm::mat4& matrix);
   void setUniformVec3(const std::string& name, const glm::dvec3& value);
   void setUniformVec4(const std::string& name, const glm::dvec4& value);
   void setUniformFloat(const std::string& name, float value);
   void setUniformInt(const std::string& name, int value);
   void setUniformUInt32(const std::string& name, unsigned int value);
   void setUniformBool(const std::string& name, bool value);
   
   ShaderProgram();
   ~ShaderProgram();
};