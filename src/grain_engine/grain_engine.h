#pragma once

#include <array>
#include <vector>
#include <cmath>
#include <random>
// #include "../plugin.hpp"
#include "../wdl/resample.h"

namespace xenakios
{
inline float clamp(float in, float low, float high)
{
    if (in<low)
        return low;
    if (in>high)
        return high;
    return in;
}
inline float rescale(float x, float xMin, float xMax, float yMin, float yMax) {
	return yMin + (x - xMin) / (xMax - xMin) * (yMax - yMin);
}
}

class GrainAudioSource
{
public:
    virtual ~GrainAudioSource() {}
    virtual float getSourceSampleRate() { return 0.0f; };
    virtual int getSourceNumSamples() { return 0; };
    virtual int getSourceNumChannels() { return 0; };
    virtual void putIntoBuffer(float* dest, int frames, int channels, int startInSource) = 0;
};

class ISGrain
{
public:
    ISGrain() 
    {
        m_grainOutBuffer.resize(65536*m_chans*2);
    }
    bool initGrain(float inputdur, float startInSource,float len, float pitch)
    {
        if (playState == 1)
            return false;
        playState = 1;
        m_outpos = 0;
        m_resampler.SetRates(m_sr , m_sr / std::pow(2.0,1.0/12*pitch));
        float* rsinbuf = nullptr;
        int lensamples = m_sr*len;
        m_grainSize = lensamples;
        m_resampler.Reset();
        int wanted = m_resampler.ResamplePrepare(lensamples,m_chans,&rsinbuf);
        
        int srcpossamples = startInSource;
        //srcpossamples+=rack::random::normal()*lensamples;
        srcpossamples = xenakios::clamp((float)srcpossamples,(float)0,inputdur-1.0f);
        m_syn->putIntoBuffer(rsinbuf,wanted,m_chans,srcpossamples);
        m_resampler.ResampleOut(m_grainOutBuffer.data(),wanted,lensamples,m_chans);
        for (int i=0;i<lensamples;++i)
        {
            float hannpos = 1.0/(m_grainSize-1)*i;
            //hannpos = fmod(hannpos+m_storedOffset,1.0f);
            float win = getWindow(hannpos,1); 
            for (int j=0;j<m_chans;++j)
            {
                m_grainOutBuffer[i*m_chans+j]*=win;
            }
            
        }
        return true;
    }
    void setNumOutChans(int chans)
    {
        m_chans = chans;
    }
    void process(float* buf)
    {
        
        for (int i=0;i<m_chans;++i)
        {
            buf[i] += m_grainOutBuffer[m_outpos*m_chans+i];
        }
        ++m_outpos;
        
        if (m_outpos>=m_grainSize)
        {
            m_outpos = 0;
            playState = 0;
        }
        
        
        
    }
    int playState = 0;
    void setSampleRate(float sr)
    {
        m_sr = sr;
    }
    
    
    float getWindow(float pos, int wtype)
    {
        if (wtype == 0)
        {
            if (pos<0.5)
                return xenakios::rescale(pos,0.0,0.5,0.0,1.0);
            return xenakios::rescale(pos,0.5,1.0 ,1.0,0.0);
        }
        else if (wtype == 1)
        {
            return 0.5f * (1.0f - std::cos(2.0f * 3.141592653 * pos));
        }
        return 0.0f;
    }
    GrainAudioSource* m_syn = nullptr;
private:
    
    int m_outpos = 0;
    int m_grainSize = 2048;
    float m_sr = 44100.0f;
    int m_chans = 2;
    WDL_Resampler m_resampler;
    std::vector<float> m_grainOutBuffer;
    
};



class GrainMixer
{
public:
    GrainAudioSource* m_syn = nullptr;
    GrainMixer(GrainAudioSource* s) : m_syn(s)
    {
        for (int i=0;i<(int)m_grains.size();++i)
        {
            m_grains[i].m_syn = s;
            m_grains[i].setNumOutChans(2);
        }
    }
    std::mt19937 m_randgen;
    std::normal_distribution<float> m_gaussdist{0.0f,1.0f};
    void processAudio(float* buf)
    {
        if (m_inputdur<0.5f)
            return;
        if (m_outcounter >= m_nextGrainPos)
        {
            float glen = m_grainDensity*2.0;
            float glensamples = m_sr*glen;
            float posrand = m_gaussdist(m_randgen)*m_posrandamt*glensamples;
            float srcpostouse = m_srcpos+posrand;
            m_grains[m_grainCounter].initGrain(m_inputdur,srcpostouse+m_loopstart*m_inputdur,glen,m_pitch);
            ++m_grainCounter;
            if (m_grainCounter==(int)m_grains.size())
                m_grainCounter = 0;
            m_nextGrainPos+=m_sr*(m_grainDensity);
            m_srcpos+=m_sr*(m_grainDensity)*m_sourcePlaySpeed;
            if (m_srcpos>=m_looplen*m_inputdur)
                m_srcpos = 0.0f;
            else if (m_srcpos<0.0f)
                m_srcpos = m_looplen*m_inputdur;
        }
        for (int i=0;i<(int)m_grains.size();++i)
        {
            if (m_grains[i].playState==1)
            {
                m_grains[i].process(buf);
            }
        }
        ++m_outcounter;
    }
    float getSourcePlayPosition()
    {
        return m_srcpos+m_inputdur*m_loopstart;
    }
    double m_srcpos = 0.0;
    float m_sr = 44100.0;
    float m_grainDensity = 0.1;
    float m_sourcePlaySpeed = 1.0f;
    float m_pitch = 0.0f; // semitones
    float m_posrandamt = 0.0f;
    float m_inputdur = 0.0f; // samples!
    float m_loopstart = 0.0f;
    float m_looplen = 1.0f;
    int m_outcounter = 0;
    int m_nextGrainPos = 0;
    int m_grainCounter = 0;
    std::array<ISGrain,2> m_grains;
};
