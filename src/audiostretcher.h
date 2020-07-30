#pragma once

#include <rack.hpp>
#include "plugin.hpp"
#include "RubberBandStretcher.h"
#ifdef RBMODULE
template<typename T>
class QeueuBuf
{
public:
    QeueuBuf(int maxsize)
    {
        m_buf.resize(maxsize);
    }
    void put(T x)
    {
        if (m_writeIndex<m_buf.size())
        {
            m_buf[m_writeIndex] = x;
            ++m_writeIndex;
        }
    }
    T get()
    {
        T result{};
        if (m_readIndex<m_buf.size())
        {
            result = m_buf[m_readIndex];
            ++m_readIndex;
        }
        return result;
    }
    int availableForWrite()
    {
        return m_buf.size()-m_writeIndex;
    }
    int availableForRead()
    {
        return m_buf.size()-m_readIndex;
    }
    void reset()
    {
        m_writeIndex = 0;
        m_readIndex = 0;
    }
private:
    int m_writeIndex = 0;
    int m_readIndex = 0;
    std::vector<T> m_buf;
};

class AudioStretchModule : public rack::Module
{
public:
    enum PARAM_IDS
    {
        PAR_PITCH_SHIFT,
        PAR_RESET,
        PAR_LAST
    };
    enum INPUTS
    {
        INPUT_AUDIO_IN,
        INPUT_PITCH_IN,
        INPUT_LAST
    };
    enum OUTPUTS
    {
        OUTPUT_AUDIO_OUT,
        OUTPUT_BUFFERAMOUNT,
        OUTPUT_LAST
    };
    AudioStretchModule();
    void process(const ProcessArgs& args) override;
    int m_rbAvailableOutput = 0;
private:
    std::unique_ptr<RubberBand::RubberBandStretcher> m_st[16];
    
    std::vector<float> m_procinbufs[16];
    std::vector<float> m_procoutbufs[16];
    dsp::ClockDivider m_paramdiv;
    int m_lastnumchans = 0;
    int m_procbufsize = 128;
    int m_procbufcounter = 0;
    bool m_lastreset = false;
};

class AudioStretchWidget : public ModuleWidget
{
public:
    AudioStretchWidget(AudioStretchModule* m);
    void draw(const DrawArgs &args) override;
    
};
#endif
