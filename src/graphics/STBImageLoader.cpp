#include "STBImageLoader.h"


#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <sstream>
#include <iostream>

STBImageLoader::STBImageLoader() {}

STBImageLoader::~STBImageLoader() {}

unsigned char* STBImageLoader::load(
   bool flipVertically, std::string path, int* width, int* height, int* nrChannels
) {
   //path = "222";
   stbi_set_flip_vertically_on_load(flipVertically);
   const char* texturePathC = path.c_str();
   unsigned char* data = stbi_load(
      texturePathC,
      width, height, nrChannels, 0
   );
   if (data) {
      return data;
   }
   std::cout << "ERROR; Failed to load texture: \"" << path << "\"." << std::endl;
   throw std::runtime_error("ERROR; Failed to load texture.");
}

void STBImageLoader::free(unsigned char* data) {
   stbi_image_free(data);
}
