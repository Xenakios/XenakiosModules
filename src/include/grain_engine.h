#pragma once

#include <array>
#include <vector>
#include <cmath>
#include <random>
#include <rack.hpp>
// #include "../plugin.hpp"
#include "../wdl/resample.h"
#include "mischelpers.h"

using namespace rack;

// adapted from https://github.com/Chowdhury-DSP/chowdsp_utils
// begin Chowdhury code
/**
    Successive samples in the delay line will be interpolated using Sinc
    interpolation. This method is somewhat less efficient than the others,
    but gives a very smooth and flat frequency response.

    Note that Sinc interpolation cannot currently be used with SIMD data types!
*/
template <typename T, size_t N, size_t M = 256>
struct Sinc
{
    Sinc()
    {
        T cutoff = 0.455f;
        size_t j;
        for (j = 0; j < M + 1; j++)
        {
            for (size_t i = 0; i < N; i++)
            {
                T t = -T (i) + T (N / (T) 2.0) + T (j) / T (M) - (T) 1.0;
                sinctable[j * N * 2 + i] = symmetric_blackman (t, (int) N) * cutoff * sincf (cutoff * t);
            }
        }
        for (j = 0; j < M; j++)
        {
            for (size_t i = 0; i < N; i++)
                sinctable[j * N * 2 + N + i] = (sinctable[(j + 1) * N * 2 + i] - sinctable[j * N * 2 + i]) / (T) 65536.0;
        }
    }

    inline T sincf (T x) const noexcept
    {
        if (x == (T) 0)
            return (T) 1;
        return (std::sin (g_pi * x)) / (g_pi * x);
    }

    inline T symmetric_blackman (T i, int n) const noexcept
    {
        i -= (n / 2);
        const double twoPi = g_pi * 2;
        return ((T) 0.42 - (T) 0.5 * std::cos (twoPi * i / (n))
                + (T) 0.08 * std::cos (4 * g_pi * i / (n)));
    }

    void reset (int newTotalSize) { totalSize = newTotalSize; }

    void updateInternalVariables (int& /*delayIntOffset*/, T& /*delayFrac*/) {}
    alignas(16) float srcbuf[N];
//#define SIMDSINC
    
    template<typename Source>
    inline T call (Source& buffer, int delayInt, double delayFrac, const T& /*state*/, int channel)
    {
        auto sincTableOffset = (size_t) (( 1.0 - delayFrac) * (T) M) * N * 2;
        
        buffer.getSamplesSafeAndFade(srcbuf,delayInt, N, channel, 512);
    #ifndef SIMDSINC
        auto out = ((T) 0);
        for (size_t i = 0; i < N; i += 1)
        {
            auto buff_reg = srcbuf[i];
            auto sinc_reg = sinctable[sincTableOffset + i];
            out += buff_reg * sinc_reg;
        }
        return out;
    #else
        alignas(16) simd::float_4 out{0.0f,0.0f,0.0f,0.0f};
        for (size_t i = 0; i < N; i += 4)
        {
            //auto buff_reg = SIMDUtils::loadUnaligned (&buffer[(size_t) delayInt + i]);
            //auto buff_reg = buffer.getBufferSampleSafeAndFade(delayInt + i,channel,512);
            alignas(16) simd::float_4 buff_reg;
            buff_reg.load(&srcbuf[i]);
            //auto sinc_reg = juce::dsp::SIMDRegister<T>::fromRawArray (&sinctable[sincTableOffset + i]);
            //auto sinc_reg = sinctable[sincTableOffset + i];
            alignas(16) simd::float_4 sinc_reg;
            sinc_reg.load(&sinctable[sincTableOffset+i]);
            out = out + (buff_reg * sinc_reg);
        }
        float sum = 0.0f;
        for (int i=0;i<4;++i)
            sum += out[i];
        return sum;
    #endif
    }

    int totalSize = 0;
    //T sinctable alignas (SIMDUtils::CHOWDSP_DEFAULT_SIMD_ALIGNMENT)[(M + 1) * N * 2];
    T sinctable alignas (16) [(M + 1) * N * 2];
};

// end Chowdhury code

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
    virtual void setSubSection(int startFrame, int endFrame) {}
    virtual float getBufferSampleSafeAndFade(int frame, int channel, int fadelen) { return 0.0f; }
    virtual void getSamplesSafeAndFade(float* destbuf,int startframe, int nsamples, int channel, int fadelen) {}
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
            m_table[i] = 0.5f * (1.0f - std::cos(2.0f * g_pi * hannpos));
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
    Sinc<float,8,512> m_sinc; // could share this between grain instances, but not yet...
    WindowLookup m_hannwind;
    ISGrain() {}
    double m_source_phase = 0.0;
    double m_source_phase_inc = 0.0;
    int m_cur_grain_len_samples = 0;
    float m_pan = 0.0f;
    bool initGrain(float inputdur, float startInSource,float len, float pitch, 
        float outsr, float pan, bool reverseGrain, int sourceFrameMin, int sourceFrameMax);
    
    float m_inputdur = 1.0f;
    void setNumOutChans(int chans)
    {
        m_chans = chans;
    }
    int* m_interpmode = nullptr;
    void process(float* buf);
    
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
            return 0.5f * (1.0f - std::cos(2.0f * g_pi * pos));
        }
        return 0.0f;
    }
    GrainAudioSource* m_syn = nullptr;
    float m_sourceplaypos = 0.0f;
    float m_cur_gain = 0.0f;
private:
    int m_outpos = 0;
    int m_grainSize = 2048;
    int m_chans = 2;
};



class GrainMixer
{
public:
    std::vector<std::unique_ptr<GrainAudioSource>>& m_sources;
    std::vector<std::unique_ptr<GrainAudioSource>> m_dummysources;
    int m_interpmode = 0;
    GrainMixer(std::vector<std::unique_ptr<GrainAudioSource>>& sources) : m_sources(sources)
    {
        for (int i=0;i<(int)m_grains.size();++i)
        {
            m_grains[i].m_syn = m_sources[0].get();
            m_grains[i].m_interpmode = &m_interpmode;
            m_grains[i].setNumOutChans(2);
        }
        m_src_pos_smoother.setParameters(dsp::BiquadFilter::LOWPASS_1POLE,1.0f/44100.0f,1.0f,1.0f);
    }
    GrainMixer(GrainAudioSource* s) : m_sources(m_dummysources)
    {
        for (int i=0;i<(int)m_grains.size();++i)
        {
            m_grains[i].m_syn = s;
            m_grains[i].m_interpmode = &m_interpmode;
            m_grains[i].setNumOutChans(2);
        }
        debugDivider.setDivision(32768);
        for (int i=0;i<16;++i) m_polypitches[i] = 0.0f;
    }
    std::mt19937 m_randgen;
    std::normal_distribution<float> m_gaussdist{0.0f,1.0f};
    std::uniform_real_distribution<float> m_unidist{0.0f,1.0f};
    int grainCounter = 0;
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
    float m_grain_trig_out = 0.0f;
    dsp::PulseGenerator m_loop_eoc_pulse;
    dsp::PulseGenerator m_grain_pulse;
    double m_grain_phasor = 1.0; // so that grain triggers immediately at start
    double m_next_randgrain = 1.0f;
    bool m_random_timing = false;
    std::pair<float,float> getGrainSourcePositionAndGain(int index)
    {
        if (index>=0 && index<m_grains.size())
        {
            if (m_grains[index].playState!=0)
                return {m_grains[index].m_sourceplaypos,m_grains[index].m_cur_gain};
        }
        return {-1.0f,0.0f};
    }
    dsp::ClockDivider debugDivider;
    dsp::BiquadFilter m_src_pos_smoother;
    float m_pitch_spread = 0.0f; 
    std::array<float,16> m_polypitches;
    int m_polypitches_to_use = 0;
    void processAudio(float* buf, float deltatime=0.0f);
    
    float getSourcePlayPosition()
    {
        return m_srcpos+m_inputdur*m_region_start;
    }
    void seekPercentage(float pos)
    {
        pos = clamp(pos,0.0f,1.0f);
        m_srcpos = m_inputdur * m_region_start + m_inputdur * pos;
    }
    double m_srcpos = 0.0;
    float m_sr = 44100.0;
    
    float m_sourcePlaySpeed = 1.0f;
    float m_pitch = 0.0f; // semitones
    float m_posrandamt = 0.0f;
    float m_inputdur = 0.0f; // samples!
    float m_region_start = 0.0f;
    float m_region_len = 1.0f;
    float m_nextLoopStart = 0.0f;
    float m_nextLoopLen = 1.0f;
    float m_loopslide = 0.0f;
    int m_outcounter = 0;
    int m_nextGrainPos = 0;
    int m_playmode = 0;
    float m_scanpos = 0.0f;
    std::array<ISGrain,10> m_grains;
    void setDensity(float d)
    {
        m_grainDensity = d;
    }
    void setLengthMultiplier(float m)
    {
        m = clamp(m,0.0f,1.0f);
        if (m<0.5f)
            m = rescale(m,0.0f,0.5f,0.5f,2.0f);
        else 
            m = rescale(m,0.5f,1.0f,2.0f,8.0f);
        
        m_lenMultip = m;
    }
private:
    float m_grainDensity = 0.1;
};
