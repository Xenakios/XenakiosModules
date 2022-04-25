#pragma once

#include <array>
#include <vector>
#include <cmath>
#include <random>
#include <rack.hpp>
// #include "../plugin.hpp"
#include "../wdl/resample.h"

using namespace rack;

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

template<typename T>
class ConcatBuffer
{
public:
    ConcatBuffer()
    {
        
    }
    void addBuffer(std::vector<T> v)
    {
        m_bufs.push_back(v);
        m_sz += v.size();
    }
    T operator[](int index)
    {
        int acc = 0;
        for (int i=0;i<(int)m_bufs.size();++i)
        {
            auto& e = m_bufs[i];
            int i0 = acc;
            int i1 = acc+e.size();
            if (index>=i0 && index<i1)
            {
                int i2 = index - acc;
                return e[i2];
            }
            acc += e.size();
        }
        return T{};
    }
    void putToBuf(T* dest, int sz, int startIndex)
    {
        // find first buffer
        int bufindex = -1;
        int acc = 0;
        for (int i=0;i<m_bufs.size();++i)
        {
            int i0 = acc;
            int i1 = acc+m_bufs[i].size();
            if (startIndex>=i0 && startIndex<i1)
            {
                bufindex = i;
                break;
            }
                
            acc+=m_bufs[i].size();
            
        }
        if (bufindex>=0)
        {
            int pos = startIndex-acc;
            for (int i=0;i<sz;++i)
            {
                
                if (pos>=m_bufs[bufindex].size())
                {
                    ++bufindex;
                    if (bufindex == m_bufs.size())
                    {
                        // reached end of buffers, fill rest of destination with default
                        for (int j=i;j<sz;++j)
                            dest[j] = T{};
                        return;
                    }
                       
                    pos = 0;
                }
                dest[i] = m_bufs[bufindex][pos];
                ++pos;
            }
        } else
        {
            for (int i=0;i<sz;++i)
                dest[i] = T{};
        }
        
    }
    int getSize()
    {
        return m_sz;
    }
private:
    std::vector<std::vector<T>> m_bufs;
    int m_sz = 0;
};

class GrainAudioSource
{
public:
    virtual ~GrainAudioSource() {}
    virtual float getSourceSampleRate() = 0;
    virtual int getSourceNumSamples() = 0;
    virtual int getSourceNumChannels() = 0;
    virtual void putIntoBuffer(float* dest, int frames, int channels, int startInSource) = 0;
};

class WindowLookup
{
public:
    WindowLookup()
    {
        m_table.resize(m_size);
        for (int i=0;i<m_size;++i)
        {
            float hannpos = 1.0/(m_size-1)*i;
            m_table[i] = 0.5f * (1.0f - std::cos(2.0f * 3.141592653 * hannpos));
        }
    }
    inline float getValue(float normpos)
    {
        int index = normpos*(m_size-1);
        return m_table[index];
    }
private:
    std::vector<float> m_table;
    int m_size = 32768;
};


class ISGrain
{
public:
    WindowLookup m_hannwind;
    ISGrain() 
    {
        m_grainOutBuffer.resize(65536*m_chans*2);
        m_srcOutBuffer.resize(65536*m_chans*2);
    }
    bool initGrain(float inputdur, float startInSource,float len, float pitch, 
        float outsr, float pan, bool reverseGrain)
    {
        if (playState == 1)
            return false;
        playState = 1;
        m_outpos = 0;
        int inchs = m_syn->getSourceNumChannels();
        float insr = m_syn->getSourceSampleRate();
        float outratio = outsr/insr;
        
        m_resampler.SetRates(insr, insr * outratio / std::pow(2.0,1.0/12*pitch));
        float* rsinbuf = nullptr;
        int lensamples = outsr*len;
        m_grainSize = lensamples;
        m_resampler.Reset();
        int wanted = m_resampler.ResamplePrepare(lensamples,inchs,&rsinbuf);
        
        int srcpossamples = startInSource;
        //srcpossamples+=rack::random::normal()*lensamples;
        srcpossamples = xenakios::clamp((float)srcpossamples,(float)0,inputdur-1.0f);
        m_sourceplaypos = 1.0f/inputdur*srcpossamples;
        m_syn->putIntoBuffer(rsinbuf,wanted,inchs,srcpossamples);
        m_resampler.ResampleOut(m_srcOutBuffer.data(),wanted,lensamples,inchs);
        float pangains[2] = {pan,1.0f-pan};
        
        if (inchs == 1 && m_chans == 2)
        {
            for (int i=0;i<lensamples;++i)
            {
                m_grainOutBuffer[i*2] = m_srcOutBuffer[i];
                m_grainOutBuffer[i*2+1] = m_srcOutBuffer[i];
            }
        } else if (inchs == 2 && m_chans == 2)
        {
            for (int i=0;i<lensamples*m_chans;++i)
            {
                m_grainOutBuffer[i] = m_srcOutBuffer[i];
            }
        }
        if (reverseGrain)
        {
            std::reverse(m_grainOutBuffer.begin(),m_grainOutBuffer.begin()+(lensamples*m_chans));

        }
        for (int i=0;i<lensamples;++i)
        {
            float hannpos = 1.0/(m_grainSize-1)*i;
            //hannpos = fmod(hannpos+m_storedOffset,1.0f);
            //float win = getWindow(hannpos,1); 
            //float win = 0.5f * (1.0f - std::cos(2.0f * 3.141592653 * hannpos));
            float win = m_hannwind.getValue(hannpos);
            for (int j=0;j<m_chans;++j)
            {
                m_grainOutBuffer[i*m_chans+j]*=win*pangains[j];
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
    inline float getWindow(float pos, int wtype)
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
    float m_sourceplaypos = 0.0f;
private:
    
    int m_outpos = 0;
    int m_grainSize = 2048;
    
    int m_chans = 2;
    WDL_Resampler m_resampler;
    std::vector<float> m_grainOutBuffer;
    std::vector<float> m_srcOutBuffer;  
};



class GrainMixer
{
public:
    std::vector<std::unique_ptr<GrainAudioSource>>& m_sources;
    std::vector<std::unique_ptr<GrainAudioSource>> m_dummysources;
    GrainMixer(std::vector<std::unique_ptr<GrainAudioSource>>& sources) : m_sources(sources)
    {
        for (int i=0;i<(int)m_grains.size();++i)
        {
            m_grains[i].m_syn = m_sources[0].get();
            m_grains[i].setNumOutChans(2);
        }
    }
    GrainMixer(GrainAudioSource* s) : m_sources(m_dummysources)
    {
        for (int i=0;i<(int)m_grains.size();++i)
        {
            m_grains[i].m_syn = s;
            m_grains[i].setNumOutChans(2);
        }
    }
    std::mt19937 m_randgen;
    std::normal_distribution<float> m_gaussdist{0.0f,1.0f};
    std::uniform_real_distribution<float> m_unidist{0.0f,1.0f};
    int debugCounter = 0;
    int findFreeGain()
    {
        for (int i=0;i<m_grains.size();++i)
        {
            if (m_grains[i].playState==0)
                return i;
        }
        return -1;
    }
    float m_actLoopstart = 0.0f;
    float m_actLoopend = 1.0f;
    float m_actSourcePos = 0.0f;
    float m_lenMultip = 1.0f;
    int m_grainsUsed = 0;
    float m_reverseProb = 0.0f;
    float m_loop_eoc_out = 0.0f;
    dsp::PulseGenerator m_loop_eoc_pulse;
    float getGrainSourcePosition(int index)
    {
        if (index>=0 && index<m_grains.size())
        {
            if (m_grains[index].playState!=0)
                return m_grains[index].m_sourceplaypos;
        }
        return -1.0f;
    }
    void processAudio(float* buf, float deltatime=0.0f)
    {
        if (m_inputdur<0.5f)
            return;
        if (m_outcounter == m_nextGrainPos)
        {
            ++debugCounter;
            m_outcounter = 0;
            float glen = m_grainDensity * m_lenMultip;
            glen = clamp(glen,0.02f,0.5f);
            //glen = rescale(glen,0.0f,1.0f,0.02f,0.5f);
            float glensamples = m_sr*glen;
            float posrand = m_gaussdist(m_randgen)*m_posrandamt*glensamples;
            float srcpostouse = m_srcpos+posrand;
            if (srcpostouse<0.0f)
                srcpostouse = 0.0f;
            srcpostouse = std::fmod(srcpostouse+m_loopslide*m_looplen*m_inputdur,m_inputdur);
            
            m_actSourcePos = srcpostouse+m_loopstart*m_inputdur;
            float pan = 0.0f;
            if (debugCounter % 2 == 1)
                pan = 1.0f;
            bool revgrain = m_unidist(m_randgen)<m_reverseProb;
            int availgrain = findFreeGain();
            //float slidedpos = std::fmod(m_srcpos+m_loopslide,1.0f);
            if (availgrain>=0)
            {
                m_grains[availgrain].initGrain(m_inputdur,srcpostouse+m_loopstart*m_inputdur,
                    glen,m_pitch,m_sr, pan, revgrain);
            }
            int usedgrains = 0;
            for (int i=0;i<m_grains.size();++i)
            {
                if (m_grains[i].playState == 1)
                    ++usedgrains;
            }
            m_grainsUsed = usedgrains;
            m_nextGrainPos=m_sr*(m_grainDensity);
            float sourceSampleRate = m_sources[0]->getSourceSampleRate();
            float rateCompens = sourceSampleRate/m_sr;
            m_srcpos+=m_sr*(m_grainDensity)*m_sourcePlaySpeed*rateCompens;
            
            float actlooplen = m_looplen; // std::pow(m_looplen,2.0f);
            float loopend = m_loopstart+actlooplen;
            
            if (loopend > 1.0f)
            {
                actlooplen -= loopend - 1.0f;
            }
            
            if (m_srcpos>=actlooplen*m_inputdur)
            {
                m_srcpos = 0.0f;
                m_loop_eoc_pulse.trigger();
            }
            else if (m_srcpos<0.0f)
            {
                m_srcpos = actlooplen*m_inputdur;
                m_loop_eoc_pulse.trigger();
            }
                
            m_actLoopstart = m_loopstart;
            m_actLoopend = m_loopstart+actlooplen;

        }
        m_loop_eoc_out = 10.0f*(float)m_loop_eoc_pulse.process(deltatime);
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
    void seekPercentage(float pos)
    {
        pos = clamp(pos,0.0f,1.0f);
        m_srcpos = m_inputdur * m_loopstart + m_inputdur * pos;
    }
    double m_srcpos = 0.0;
    float m_sr = 44100.0;
    
    float m_sourcePlaySpeed = 1.0f;
    float m_pitch = 0.0f; // semitones
    float m_posrandamt = 0.0f;
    float m_inputdur = 0.0f; // samples!
    float m_loopstart = 0.0f;
    float m_looplen = 1.0f;
    float m_loopslide = 0.0f;
    int m_outcounter = 0;
    int m_nextGrainPos = 0;
    
    std::array<ISGrain,64> m_grains;
    void setDensity(float d)
    {
        if (d!=m_grainDensity)
        {
            m_grainDensity = d;
            //m_nextGrainPos = m_outcounter;
        }
    }
    void setLengthMultiplier(float m)
    {
        m = rescale(m,0.0f,1.0f,0.5f,8.0f);
        m_lenMultip = clamp(m,0.5f,8.0f);
    }
private:
    float m_grainDensity = 0.1;
};
