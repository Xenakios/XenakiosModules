#pragma once

#include <algorithm>

#include "dr_wav.h"

class DrWavBuffer
{
public:
    DrWavBuffer() {}
    DrWavBuffer(float* src, drwav_uint64 numFrames)
    {
        m_buf = src;
        m_sz = numFrames;
    }
    ~DrWavBuffer()
    {
        if (m_buf)
            drwav_free(m_buf, nullptr);
    }
    DrWavBuffer(const DrWavBuffer&) = delete;
    DrWavBuffer& operator=(const DrWavBuffer&) = delete;
    DrWavBuffer(DrWavBuffer&& other)
    {
        m_buf = other.m_buf;
        m_sz = other.m_sz;
        other.m_buf = nullptr;
        other.m_sz = 0;
    }
    DrWavBuffer& operator=(DrWavBuffer&& other)
    {
        std::swap(m_buf,other.m_buf);
        std::swap(m_sz,other.m_sz);
        return *this;
    }
    float* data() { return m_buf; }
    drwav_uint64 size() { return m_sz; }
private:
    float* m_buf = nullptr;
    drwav_uint64 m_sz = 0;
};
