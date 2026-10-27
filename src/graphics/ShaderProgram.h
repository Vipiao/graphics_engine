#pragma once

#include <array>
#include <glad/glad.h>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <utility>

class ShaderProgram {
public:
   // Maximum number of texture units supported by the shaders. Injected into every shader
   // as "#define MAX_TEXTURE_UNITS", so shaders must declare
   // "uniform sampler2D u_textures[MAX_TEXTURE_UNITS];" instead of a literal size.
   // The GL spec only guarantees GL_MAX_TEXTURE_IMAGE_UNITS >= 16.
   static constexpr int s_maxTextureUnits{ 32 };

private:
   // One entry per pipeline stage, indexed by the s_*Stage constants below.
   // A stage owns its GL shader object until the program is linked.
   struct Stage {
      unsigned int m_shader{ 0 };
      bool m_isLoaded{ false };
      std::string m_path{};  // kept across reloads; empty for string-loaded stages
   };

   static constexpr size_t s_vertexStage{ 0 };
   static constexpr size_t s_fragmentStage{ 1 };
   static constexpr size_t s_tessControlStage{ 2 };
   static constexpr size_t s_tessEvaluationStage{ 3 };
   static constexpr size_t s_geometryStage{ 4 };
   // A compute program has only this stage; it runs with glDispatchCompute
   // instead of drawing.
   static constexpr size_t s_computeStage{ 5 };
   static constexpr size_t s_stageCount{ 6 };

   // Inserts the engine "#define"s (see s_maxTextureUnits), then this program's
   // own (see addDefine), after the "#version" line.
   std::string injectDefines(std::string code) const;

   // Recursively expands '#include "path"' lines, resolving paths relative to the
   // including file. Each file is expanded at most once per top-level load, so
   // repeated (and cyclic) includes expand to nothing.
   static std::string expandIncludes(
      const std::string& path, std::vector<std::string>& includedPaths);

   // Throws unless the loaded stages make a program: either the compute stage
   // alone, or at least a vertex and a fragment stage.
   void checkStagesCanLink() const;

   // Compiles code as the given stage; throws on compile errors.
   void loadStage(size_t stageIndex, std::string code);
   // Loads the file, remembers its path for reloadShaders, and compiles.
   void loadStageFromPath(size_t stageIndex, std::string path);

   // Location of every u_textures[i] in the linked program, resolved once at
   // link time. This class injects MAX_TEXTURE_UNITS and so dictates the array's
   // name, which makes it the right place to resolve it: a draw can then reach a
   // sampler by index without building "u_textures[i]" to ask the driver for a
   // location that has not changed since the program linked. -1 where the
   // program does not sample that unit.
   std::array<GLint, s_maxTextureUnits> m_textureUnitLocations{};

   // Fills m_textureUnitLocations from the linked program.
   void cacheTextureUnitLocations();

   std::array<Stage, s_stageCount> m_stages{};
   // "#define" lines added by addDefine, injected into every stage.
   std::string m_defines{};
   unsigned int m_shaderProgram{ 0 };
   bool m_programIsLinked{ false };
   bool m_hasStoredPaths{ false };

public:
   // Loads a shader source file with '#include "path"' lines expanded (see
   // expandIncludes). Include paths are relative to the file that includes them.
   static std::string loadTextFileFromPath(std::string path);
   static void printWithLineNumbers(std::string code);
   // In every stage loaded after this call, and on every reload.
   void addDefine(const std::string& name, const std::string& value);
   void loadVertexShaderFromPath(std::string vertexCodePath);
   void loadFragmentShaderFromPath(std::string fragmentCodePath);
   void loadTessellationControlShaderFromPath(std::string tessContrCodePath);
   void loadTessellationEvaluationShaderFromPath(std::string tessEvalCodePath);
   void loadGeometryShaderFromPath(std::string geometryCodePath);
   void loadComputeShaderFromPath(std::string computeCodePath);
   void loadVertexShader(std::string vertexCode);
   void loadFragmentShader(std::string fragmentCode);
   void loadTessellationControlShader(std::string tessContrCode);
   void loadTessellationEvaluationShader(std::string tessEvalCode);
   void loadGeometryShader(std::string geometryCode);
   void loadComputeShader(std::string computeCode);
   void linkShaders();
   unsigned int getID();
   void use();

   // Where u_textures[unit] lives in this program, or -1 if it does not sample
   // it. Valid after linkShaders; refreshed by reloadShaders.
   GLint getTextureUnitLocation(int unit) const;

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

   // Owns GL shader/program handles; copying would double-delete them.
   ShaderProgram(const ShaderProgram&) = delete;
   ShaderProgram& operator=(const ShaderProgram&) = delete;
};
