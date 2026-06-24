#pragma once


#include <string>


class STBImageLoader {
protected:
public:
   STBImageLoader();
   ~STBImageLoader();

   static unsigned char* load(
      bool flipVertically, std::string path, int* width, int* height, int* nrChannels
   );
   static void free(unsigned char* data);
};