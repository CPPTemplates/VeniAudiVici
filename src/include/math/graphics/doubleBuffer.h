#include "texture.h"
#pragma once
struct doubleBuffer
{
    //the index used for writing
    static inline constexpr int write = 0;
    //the index used for reading
    static inline constexpr int read = 1;
    doubleBuffer() {}
    //CAUTION! any textures left in the buffer will be deleted upon destruction!
    doubleBuffer(cveci2 &size)
    {
        for (texture *&tex : buffer)
        {
            tex = new texture(size);
        }
    }
    inline void swap()
    {
        thread0DoubleBufferIndex = 1 - thread0DoubleBufferIndex;
    }
    // 2 contexts and rendertextures, 1 for each thread. this is to prevent contexts from being created and deleted constantly
    byte thread0DoubleBufferIndex = 0;
    texture *buffer[2]{};
    texture *&operator[](cbyte &threadNumber)
    {
        return buffer[threadNumber ^ thread0DoubleBufferIndex];
    }
    ~doubleBuffer()
    {
        for (texture *&tex : buffer)
        {
            delete tex;
        }
    }
};