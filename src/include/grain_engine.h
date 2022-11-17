#pragma once

#include <array>
#include <vector>
#include <cmath>
#include <random>
#include <memory>
#ifdef RAPIHEADLESS
//#include <simd/Vector.hpp>
//#include <simd/functions.hpp>
#include <dsp/common.hpp>
#include <dsp/filter.hpp>
#include <dsp/digital.hpp>
#include <dsp/fir.hpp>
#include <math.hpp>
#else
#include <rack.hpp>
#include "dr_wav.h"
#include <iostream>
#endif
//#include <rack.hpp>
// #include "../plugin.hpp"
// #include "../wdl/resample.h"
#include "mischelpers.h"

using namespace rack;
using namespace rack::math;

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
    inline T call (Source& buffer, int delayInt, double delayFrac, const T& /*state*/, int channel, int sposmin, int sposmax)
    {
        auto sincTableOffset = (size_t) (( 1.0 - delayFrac) * (T) M) * N * 2;
        
        buffer.getSamplesSafeAndFade(srcbuf,delayInt, N, channel, sposmin, sposmax, 512);
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
    virtual float getBufferSampleSafeAndFade(int frame, int channel, int minFramePos, int maxFramePos, int fadelen) { return 0.0f; }
    virtual void getSamplesSafeAndFade(float* destbuf,int startframe, int nsamples, int channel, int minFramePos, int maxFramepos, int fadelen) {}
};

class MultiBufferSource : public GrainAudioSource
{
    std::vector<std::vector<float>> m_audioBuffers;
    int m_playbackBufferIndex = 0;
    int m_recordBufferIndex = 0;
    std::vector<int> m_recordBufPositions;
public:
    MultiBufferSource()
    {
        int numbufs = 5;
        m_recordBufPositions.resize(numbufs);
        m_recordStartPositions.resize(numbufs);
        m_media_cues.resize(numbufs);
        m_has_recorded.resize(numbufs);
        m_audioBuffers.resize(numbufs);
        for (auto& e : m_audioBuffers)
            e.resize(44100*300*2);
        peaksData.resize(2);
        for (auto& e : peaksData)
            e.resize(1024*1024);

    }
    unsigned int m_channels = 0;
    unsigned int m_sampleRate = 44100;
    drwav_uint64 m_totalPCMFrameCount = 0;
    
    int m_recordChannels = 0;
    float m_recordSampleRate = 0.0f;
    int m_recordState = 0;
    
    spinlock m_mut;
    
    std::atomic<int> m_do_update_peaks{0};
    std::string m_filename;
#ifndef RAPIHEADLESS
    void normalize(float level, int startframe, int endframe)
    {
        /*
        if (startframe == -1 && endframe == -1)
        {
            startframe = 0;
            endframe = m_audioBuffer.size() / m_channels;
        }
        auto framesToUse = endframe-startframe;
        int chanstouse = m_channels;
        float* dataToUse = m_audioBuffer.data();
        float peak = 0.0f;
        for (int i=0;i<framesToUse;++i)
        {
            int index = startframe+i;
            for (int j=0;j<chanstouse;++j)
            {
                float s = std::fabs(dataToUse[index*chanstouse+j]);
                peak = std::max(s,peak);
            }
            
        }
        float normfactor = 1.0f;
        if (peak>0.0f)
            normfactor = level/peak;
        for (int i=0;i<framesToUse;++i)
        {
            int index = startframe+i;
            for (int j=0;j<chanstouse;++j)
            {
                dataToUse[index*chanstouse+j] *= normfactor;
            }
        }
        m_do_update_peaks = true;
        */
    }

    void reverse()
    {
        
        for (int i=0;i<m_totalPCMFrameCount/2;i++)
        {
            int index=(m_totalPCMFrameCount-i-1);
            if (index<0 || index>=m_totalPCMFrameCount) break;
            for (int j=0;j<m_channels;j++)
            {
                //std::swap(m_pSampleData[i*m_channels+j],m_pSampleData[index*m_channels+j]);
            }
        }
        updatePeaks();
        
    }
#endif    
    std::mutex m_peaks_mut;
    int m_peak_updates_counter = 0;
    int m_minFramePos = 0;
    int m_maxFramePos = 0;
#ifndef RAPIHEADLESS
    void updatePeaks()
    {
        if (m_do_update_peaks == 0)
            return;
        m_peak_updates_counter++;
        std::lock_guard<std::mutex> locker(m_peaks_mut);
        float* dataPtr = m_audioBuffers[0].data();
        peaksData.resize(m_channels);
        int samplesPerPeak = 256;
        int numPeaks = m_totalPCMFrameCount/(float)samplesPerPeak;
        for (int i=0;i<m_channels;++i)
        {
            peaksData[i].resize(numPeaks);
        }
        for (int i=0;i<m_channels;++i)
        {
            int sampleCounter = 0;
            for (int j=0;j<numPeaks;++j)
            {
                float minsample = std::numeric_limits<float>::max();
                float maxsample = std::numeric_limits<float>::min();
                for (int k=0;k<samplesPerPeak;++k)
                {
                    int index = sampleCounter*m_channels+i;
                    float sample = 0.0f;
                    if (index<m_totalPCMFrameCount*m_channels)
                        sample = dataPtr[index];
                    minsample = std::min(minsample,sample);
                    maxsample = std::max(maxsample,sample);
                    ++sampleCounter;
                }
                peaksData[i][j].minpeak = minsample;
                peaksData[i][j].maxpeak = maxsample;
            }
        } 
        m_do_update_peaks = 0;
    }
#endif
    bool saveFile(std::string filename, int whichbuffer)
    {
#ifndef RAPIHEADLESS
        drwav wav;
        drwav_data_format format;
		format.container = drwav_container_riff;
		format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
		format.channels = 2;
		format.sampleRate = m_sampleRate;
		format.bitsPerSample = 32;
        if (drwav_init_file_write(&wav,filename.c_str(),&format,nullptr))
        {
            drwav_write_pcm_frames(&wav,m_audioBuffers[whichbuffer].size()/2,
                (void*)m_audioBuffers[whichbuffer].data());
            drwav_uninit(&wav);
            return true;
        }
        return false;
#else
        SF_INFO sinfo;
        memset(&sinfo,0,sizeof(SF_INFO));
        sinfo.channels = 2;
        sinfo.format = SF_FORMAT_PCM_16 | SF_FORMAT_WAV;
        sinfo.samplerate = m_sampleRate;
        SNDFILE* outfile = sf_open(filename.c_str(),SFM_WRITE,&sinfo);
        if (outfile)
        {
            sf_writef_float(outfile,m_audioBuffers[whichbuffer].data(),m_audioBuffers[whichbuffer].size()/2);
            sf_close(outfile);
            return true;
        }
        return false;
#endif
    }
    std::vector<std::vector<uint32_t>> m_media_cues; 
    bool importFile(std::string filename, int whichbuffer)
    {
        if (whichbuffer<0 || whichbuffer>=m_audioBuffers.size())
            return false;
        int sr = 0;
#ifndef RAPIHEADLESS
        drwav_uint64 totalPCMFrameCount = 0;
        drwav wav;
        if (!drwav_init_file(&wav, filename.c_str(), nullptr))
            return false;
        int framestoread = std::min(m_audioBuffers[whichbuffer].size()/2,(size_t)wav.totalPCMFrameCount);
        int inchs = wav.channels;
        sr = wav.sampleRate;
        if (inchs==2)
        {
            drwav_read_pcm_frames_f32(&wav, framestoread, m_audioBuffers[whichbuffer].data());
		    drwav_uninit(&wav);
        }
        if (inchs == 1)
        {
            std::vector<float> temp(inchs*framestoread);
            drwav_read_pcm_frames_f32(&wav, framestoread, temp.data());
            for (int i=0;i<framestoread;++i)
            {
                if (inchs == 1)
                {
                    for (int j=0;j<2;++j)
                    {
                        m_audioBuffers[whichbuffer][i*2+j] = temp[i];
                    }
                } 
            }
            drwav_uninit(&wav);
        }
        
        
#else
        SF_INFO sinfo;
        SNDFILE* sfile = sf_open(filename.c_str(),SFM_READ,&sinfo);
        if (!sfile)
            return false;
        int framestoread = std::min(m_audioBuffers[whichbuffer].size()/2,(size_t)sinfo.frames);
        int inchs = sinfo.channels;
        sr = sinfo.samplerate;
        SF_CUES cues;
        
        if (sf_command(sfile,SFC_GET_CUE,&cues,sizeof(SF_CUES)) == SF_TRUE)
        {
            m_media_cues[whichbuffer].clear();
            std::cout << "found media cues for file " << filename << "\n";
            std::cout << cues.cue_count << " cues\n";
            for (int i=0;i<cues.cue_count;++i)
            {
                m_media_cues[whichbuffer].push_back(cues.cue_points[i].sample_offset);
                //std::cout << cues.cue_points[i].sample_offset << " ";
            }
                
            //std::cout << "\n";
        }
        if (inchs != 2)
        {
            std::vector<float> temp(inchs*framestoread);
            sf_readf_float(sfile,temp.data(),framestoread);
            for (int i=0;i<framestoread;++i)
            {
                if (inchs == 1)
                {
                    for (int j=0;j<2;++j)
                    {
                        m_audioBuffers[whichbuffer][i*2+j] = temp[i];
                    }
                } 
            }
        } else
        {
            sf_readf_float(sfile,m_audioBuffers[whichbuffer].data(),framestoread);
        }
        sf_close(sfile);
#endif
        m_mut.lock();
            m_channels = 2;
            m_sampleRate = sr;
            m_totalPCMFrameCount = framestoread;
            m_recordState = 0;
        
        m_mut.unlock();
        
        m_do_update_peaks = 1;
        m_filename = filename;
        return true;
    }
    void copyBuffer(int sourceBufferIndex,int destBufferIndex)
    {
        if (sourceBufferIndex == destBufferIndex)
            return;
        if (sourceBufferIndex>=0 && sourceBufferIndex < m_audioBuffers.size() &&
            destBufferIndex>=0 && destBufferIndex < m_audioBuffers.size())
        {
            std::copy(m_audioBuffers[sourceBufferIndex].begin(),
                m_audioBuffers[sourceBufferIndex].end(),m_audioBuffers[destBufferIndex].begin());
        }
    }
    std::atomic<int> busy_state{0};
    struct SamplePeaks
    {
        float minpeak = 0.0f;
        float maxpeak = 0.0f;
    };
    std::vector<std::vector<SamplePeaks>> peaksData;
    
    void clearAudio(int startSample, int endSample, int whichbuffer)
    {
        
        if (startSample == -1 && endSample == -1)
        {
            startSample = 0;
            endSample = (m_audioBuffers[whichbuffer].size() / m_channels)-1;
        }
        startSample = startSample*m_channels;
        endSample = endSample*m_channels;
        if (startSample>=0 && endSample<m_audioBuffers[whichbuffer].size())
        {
            for (int i=startSample;i<endSample;++i)
            {
                m_audioBuffers[whichbuffer][i] = 0.0f;
            }
            m_do_update_peaks = 1;
        }
    }
    void resetRecording()
    {
        if (m_recordBufPositions[m_recordBufferIndex] >= m_audioBuffers[m_recordBufferIndex].size())
        {
            m_recordBufPositions[m_recordBufferIndex] = 0;
        }
    }
    std::vector<bool> m_has_recorded;
    std::vector<int> m_recordStartPositions;
    std::pair<float,float> getLastRecordedRange()
    {
        auto& recbuf = m_audioBuffers[m_recordBufferIndex];
        float s0 = rescale((float)m_recordStartPositions[m_recordBufferIndex],0.0f,(float)recbuf.size()-1,0.0f,1.0f);
        float s1 = rescale((float)m_recordBufPositions[m_recordBufferIndex],0.0f,(float)recbuf.size()-1,0.0f,1.0f);
        return {s0,s1};
    }
    void startRecording(int numchans, float sr)
    {
        if (m_recordState!=0)
            return;
        m_has_recorded[m_recordBufferIndex] = true;
        m_recordChannels = numchans;
        m_recordSampleRate = sr;
        m_recordState = 1;
        m_recordStartPositions[m_recordBufferIndex] = m_recordBufPositions[m_recordBufferIndex];
    }
    int m_odub_minpos_frames = 0;
    int m_odub_maxpos_frames = 0;
    void startOverDubbing(int numchans, float sr, int startPosFrames, int minposFrames, int maxposFrames)
    {
        m_odub_minpos_frames = minposFrames;
        m_odub_maxpos_frames = maxposFrames;
        m_has_recorded[m_recordBufferIndex] = true;
        m_recordChannels = numchans;
        m_recordSampleRate = sr;
        m_recordState = 2;
        m_recordStartPositions[m_recordBufferIndex] = clamp(startPosFrames,minposFrames,maxposFrames);
        m_recordBufPositions[m_recordBufferIndex] = m_recordStartPositions[m_recordBufferIndex];
    }
    void pushSamplesToRecordBuffer(float* samples, float gain, int whichbuffer=-1, bool force=false)
    {
        if (whichbuffer == -1)
            whichbuffer = m_recordBufferIndex;
        if (m_recordState == 0 && force == false)
            return;
        m_has_recorded[whichbuffer] = true;
        auto& recbuf = m_audioBuffers[whichbuffer];
        auto& recpos = m_recordBufPositions[whichbuffer];
        for (int i=0;i<m_recordChannels;++i)
        {
            if (recpos < recbuf.size())
            {
                if (m_recordState == 1)
                    recbuf[recpos] = samples[i]*gain;
                else if (m_recordState == 2)
                    recbuf[recpos] += samples[i]*gain;
            }
            ++recpos;
            if (m_recordState == 1 && recpos == recbuf.size())
            {
                std::cout << "RECORD BUFFER FULL, STOPPING\n";
                stopRecording();
                break;
            }
            if (m_recordState == 2 && recpos == m_odub_maxpos_frames)
            {
                recpos = m_odub_minpos_frames;
            }
        }
    }
    float getRecordPosition()
    {
        if (m_recordState == 0)
            return -1.0f;
        return 1.0/m_audioBuffers[m_recordBufferIndex].size()*m_recordBufPositions[m_recordBufferIndex];
    }
    void stopRecording()
    {
        m_recordState = 0;
        m_channels = m_recordChannels;
        m_sampleRate = m_recordSampleRate;
        m_totalPCMFrameCount = m_audioBuffers[m_recordBufferIndex].size()/m_recordChannels;
        m_do_update_peaks = 1;
    }
    void setPlaybackBufferIndex(int which)
    {
        if (which>=0 && which<m_audioBuffers.size())
        {
            m_playbackBufferIndex = which;
        }
    }
    int getPlaybackBufferIndex() const
    {
        return m_playbackBufferIndex;
    }
    int getRecordBufferIndex() const
    {
        return m_recordBufferIndex;
    }
    void setRecordBufferIndex(int which)
    {
        if (which>=0 && which<m_audioBuffers.size())
        {
            m_recordBufferIndex = which;
        }
    }
    void getSamplesSafeAndFade(float* destbuf,int startframe,int nsamples, int channel, 
        int minFramepos, int maxFramePos, int fadelen) override
    {
        for (int i=0;i<nsamples;++i)
        {
            destbuf[i] = getBufferSampleSafeAndFadeImpl(startframe+i,channel, minFramepos, maxFramePos, fadelen);
        }
    }
    float getBufferSampleSafeAndFade(int frame, int channel, int minFramePos, int maxFramePos, int fadelen) override final
    {
        return getBufferSampleSafeAndFadeImpl(frame,channel, minFramePos, maxFramePos, fadelen);
    }
    // OK, probably not the most efficient implementation, but will have to see later if can be optimized
    float getBufferSampleSafeAndFadeImpl(int frame, int channel, int minFramePos, int maxFramePos, int fadelen) 
    {
        if (__builtin_expect(frame>=0 && frame < m_totalPCMFrameCount,1))
        {
            float gain = 1.0f;
            if (frame>=minFramePos && frame<minFramePos+fadelen)
                gain = rescale((float)frame,minFramePos,minFramePos+fadelen,0.0f,1.0f);
            if (frame>=maxFramePos-fadelen && frame<maxFramePos)
                gain = rescale((float)frame,maxFramePos-fadelen,maxFramePos,1.0f,0.0f);
            if (frame<minFramePos || frame>=maxFramePos)
                gain = 0.0f;
            return m_audioBuffers[m_playbackBufferIndex][frame*m_channels+channel] * gain;
        }
        return 0.0;
    }
    void putIntoBuffer(float* dest, int frames, int channels, int startInSource) override
    {
        std::lock_guard<spinlock> locker(m_mut);
        if (m_channels==0)
        {
            for (int i=0;i<frames*channels;++i)
                dest[i]=0.0f;
            return;
        }
        float* srcDataPtr = m_audioBuffers[m_playbackBufferIndex].data();
        const int srcchanmap[4][4]=
        {
            {0,0,0,0},
            {0,1,0,1},
            {0,1,2,0},
            {0,1,2,3}
        };
        int fadelen = m_sampleRate * 0.005f;
        int subsectlen = m_maxFramePos-m_minFramePos;
        for (int i=0;i<frames;++i)
        {
            int index = (i+startInSource) ;
            index = wrap_value_safe(m_minFramePos,index,m_maxFramePos);
            for (int j=0;j<channels;++j)
            {
                int actsrcchan = srcchanmap[m_channels-1][j];
                dest[i*channels+j] = getBufferSampleSafeAndFade(index,actsrcchan,0,m_totalPCMFrameCount, fadelen);
            }
        }
    }
    ~MultiBufferSource()
    {
        for (int i=0;i<m_has_recorded.size();++i)
        {
            if (m_has_recorded[i])
            {
    #ifndef RAPIHEADLESS
                std::string audioDir = rack::asset::user("XenakiosGrainAudioFiles");
                uint64_t t = system::getUnixTime();
                std::string audioFile = audioDir+"/GrainRec_"+std::to_string(t)+".wav";
                saveFile(audioFile,0);
    #else
                std::cout << "saving audio buffer " << i << "...\n";
                std::string audioDir = "/home/pi/codestuff/XenakiosModules/src/audiomodules/reels";
                //auto t = std::chrono::system_clock::now().time_since_epoch().count();
                std::string audioFile = audioDir+"/reel_"+std::to_string(i)+".wav";
                saveFile(audioFile,i);
                std::cout << "finished saving audio buffer\n";
    #endif
            }
        }
    }
    int getSourceNumChannels() override
    {
        return m_channels;
    }
    float getSourceSampleRate() override 
    { 
        return m_sampleRate;
    }
    int getSourceNumSamples() override { return m_totalPCMFrameCount; };
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

class BufferScrubber
{
public:
    Sinc<float,16,65536> m_sinc_interpolator;
    
    BufferScrubber(MultiBufferSource* src) : m_src{src}
    {
        m_filter_divider.setDivision(128);
        updateFiltersIfNeeded(44100.0f,20.0f, true);
    }
    std::array<double,2> m_last_pos = {0.0f,0.0f};
    int m_resampler_type = 1;
    int m_compensate_volume = 0;
    float m_last_sr = 0.0f;
    float m_last_pos_smoother_cutoff = 0.0f;
    dsp::ClockDivider m_filter_divider;
    void updateFiltersIfNeeded(float sr, float scrubSmoothingCutoff, bool force = false)
    {
        if (force || m_filter_divider.process())
        {
            if (sr!=m_last_sr || m_last_pos_smoother_cutoff!=scrubSmoothingCutoff)
            {
                for(auto& f : m_position_smoothers) 
                    f.setParameters(dsp::BiquadFilter::LOWPASS_1POLE,scrubSmoothingCutoff/sr,1.0,1.0f);
                for(auto& f : m_gain_smoothers) 
                    f.setParameters(dsp::BiquadFilter::LOWPASS_1POLE,8.0/sr,1.0,1.0f);
                m_last_sr = sr;
                m_last_pos_smoother_cutoff = scrubSmoothingCutoff;
            }
        }
    }
    void processFrame(float* outbuf, int nchs, float sr, float scansmoothingCutoff)
    {
        updateFiltersIfNeeded(sr, scansmoothingCutoff);
        double positions[2] = {m_next_pos-m_separation,m_next_pos+m_separation};
        int srcstartsamples = m_src->getSourceNumSamples() * m_reg_start;
        int srcendsamples = m_src->getSourceNumSamples() * m_reg_end;
        int srclensamps = srcendsamples - srcstartsamples;
        //m_src->setSubSection(srcstartsamples,srcendsamples);
        for (int i=0;i<2;++i)
        {
            double target_pos = m_position_smoothers[i].process(positions[i]);
            m_smoothed_positions[i] = m_reg_start + target_pos * (m_reg_end-m_reg_start);
            
            double temp = (double)srcstartsamples + target_pos * srclensamps;
            double posdiff = m_last_pos[i] - temp;
            double posdiffa = std::abs(posdiff);
            if (posdiffa<1.0f/128) // so slow already that can just as well cut output
            {
                m_out_gains[i] = 0.0;
            } else
            {
                m_out_gains[i] = 1.0f;
                if (m_compensate_volume == 1 && posdiffa>=4.0)
                {
                    m_out_gains[i] = rescale(posdiffa,4.0f,32.0f,1.0f,0.0f);
                    if (m_out_gains[i]<0.0f)
                        m_out_gains[i] = 0.0f;
                }
                    
            }
            m_last_pos[i] = temp;
            int index0 = temp;
            int index1 = index0+1;
            double frac = (temp - (double)index0); 
            if (m_resampler_type == 1) // for sinc...
                frac = 1.0-frac;
            float gain = m_gain_smoothers[i].process(m_out_gains[i]);
            if (gain>=0.00001) // over -100db, process
            {
                m_stopped = false;
                float bogus = 0.0f;
                if (m_resampler_type == 0)
                {
                    float y0 = m_src->getBufferSampleSafeAndFade(index0,i,srcstartsamples,srcendsamples, 256);
                    float y1 = m_src->getBufferSampleSafeAndFade(index1,i,srcstartsamples,srcendsamples, 256);
                    float y2 = y0+(y1-y0)*frac;
                    outbuf[i] = y2 * gain;
                }    
                else
                {
                    float y2 = m_sinc_interpolator.call(*m_src,index0,frac,bogus,i,srcstartsamples,srcendsamples);
                    outbuf[i] = y2 * gain;
                    
                }
            } else
            {
                m_stopped = true;
                outbuf[i] = 0.0f;
            }
        }
        
    }
    void setNextPosition(double npos)
    {
        m_next_pos = npos;
    }
    void setRegion(float startpos, float endpos)
    {
        m_reg_start = startpos;
        m_reg_end = endpos;
    }
    void setSeparation(float s)
    {
        m_separation = rescale(s,0.0f,1.0f,-0.1f,0.1f);
    }
    float m_separation = 0.0f;
    float m_reg_start = 0.0f;
    float m_reg_end = 1.0f;
    bool m_stopped = false;
    double m_cur_pos = 0.0;
    std::array<float,2> m_smoothed_positions = {0.0f,0.0f};
    std::array<float,2> m_out_gains = {0.0f,0.0f};
    double m_smoothed_out_gain = 0.0f;
private:
    MultiBufferSource* m_src = nullptr;
    
    double m_next_pos = 0.0f;
    std::array<dsp::BiquadFilter,2> m_gain_smoothers;
    std::array<dsp::BiquadFilter,2> m_position_smoothers;
     
};


class ISGrain
{
public:
    Sinc<float,8,512>* m_sinc = nullptr; 
    WindowLookup* m_hannwind = nullptr;
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
    int m_sourceFrameMin = 0;
    int m_sourceFrameMax = 1;
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
            m_grains[i].m_sinc = &m_sinc;
            m_grains[i].m_hannwind = &m_hannwind;
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
            m_grains[i].m_sinc = &m_sinc;
            m_grains[i].m_hannwind = &m_hannwind;
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
    inline std::pair<float,float> getGrainSourcePositionAndGain(int index)
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
    Sinc<float,8,512> m_sinc;
    WindowLookup m_hannwind;
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

class GrainEngine
{
public:
    std::unique_ptr<BufferScrubber> m_scrubber;
    GrainEngine()
    {
        MultiBufferSource* wavsrc = new MultiBufferSource;
        m_srcs.emplace_back(wavsrc);
        m_gm.reset(new GrainMixer(m_srcs));
        m_markers = {0.0f,1.0f};
        m_scrubber.reset(new BufferScrubber(wavsrc));
    }
    bool isRecording()
    {
        auto src = dynamic_cast<MultiBufferSource*>(m_srcs[0].get());
        return src->m_recordState>0;
    }
    std::pair<float,float> getActiveRegionRange()
    {
        return {m_reg_start,m_reg_end};
    }
    float m_reg_start = 0.0f;
    float m_reg_end = 1.0f;
    std::vector<float> m_markers;
    bool m_marker_added = false;
    float m_marker_add_pos = 0.0f;
    void addMarker()
    {
        float insr = m_gm->m_sources[0]->getSourceSampleRate();
        float inlensamps = m_gm->m_sources[0]->getSourceNumSamples();
        float inlensecs = insr * inlensamps;
        float tpos = 1.0f/inlensamps*m_gm->m_srcpos;
        tpos += m_gm->m_region_start;
        tpos = clamp(tpos,0.0f,1.0f);
        m_markers.push_back(tpos);
        m_marker_added = true;
        m_marker_add_pos = tpos;
        std::sort(m_markers.begin(),m_markers.end());
        
        //m_gm->m_srcpos = 0.0f;
    }
    void addEquidistantMarkers(int nummarkers)
    {
        m_markers.clear();
        for (int i=0;i<nummarkers+1;++i)
        {
            float tpos = 1.0f/nummarkers*i;
            m_markers.push_back(tpos);
        }
    }
    void addMarkerAtPosition(float pos)
    {
        pos = clamp(pos,0.0f,1.0f);
        m_markers.push_back(pos);
        std::sort(m_markers.begin(),m_markers.end());
        auto last = std::unique(m_markers.begin(), m_markers.end());
        m_markers.erase(last, m_markers.end());
    }
    void clearMarkers()
    {
        m_markers = {0.0f,1.0f};
    }
    float getCurrentRegionAsPercentage()
    {
        return clamp(1.0f/(m_markers.size()-1)*m_chosen_region,0.0f,1.0f);
    }
    int m_chosen_region = 0;
    void process(float deltatime, float sr,float* buf, float playrate, float pitch, 
        float loopstart, float looplen, float loopslide,
        float posrand, float grate, float lenm, float revprob, int ss, float pitchspread)
    {
        buf[0] = 0.0f;
        buf[1] = 0.0f;
        buf[2] = 0.0f;
        buf[3] = 0.0f;
        
        m_gm->m_sr = sr;
        m_gm->m_inputdur = m_srcs[0]->getSourceNumSamples();
        m_gm->m_pitch_spread = pitchspread;
        int markerIndex = ((m_markers.size()-1)*loopstart);
        markerIndex = clamp(markerIndex,0,m_markers.size()-2);
        m_chosen_region = markerIndex;
        float regionStart = m_markers[markerIndex];
        ++markerIndex;
        if (markerIndex >= m_markers.size())
            markerIndex = m_markers.size() - 1;
        float regionEnd = m_markers[markerIndex];
        float regionLen = regionEnd - regionStart; 
        m_reg_start = regionStart;
        m_reg_end = regionEnd;
        regionLen = clamp(regionLen,0.0f,1.0f);
    
        m_gm->m_sourcePlaySpeed = playrate;
        m_gm->m_pitch = pitch;
        m_gm->m_posrandamt = posrand;
        m_gm->m_reverseProb = revprob;
        m_gm->setDensity(grate);
        m_gm->setLengthMultiplier(lenm);
        m_gm->m_nextLoopStart = regionStart;
        m_gm->m_nextLoopLen = looplen * regionLen;
        m_gm->m_loopslide = loopslide;
        m_gm->m_playmode = m_playmode;
        m_gm->m_scanpos = m_scanpos;
        if (m_playmode == 2)
        {
            m_scrubber->setRegion(regionStart,regionEnd);
            m_scrubber->setNextPosition(m_scanpos);
            float scrubsmoothcutoff = rescale(std::pow(lenm,2.5f),0.0f,1.0f,0.1f,16.0f);
            m_scrubber->processFrame(buf,2,sr,scrubsmoothcutoff);
            return;
        }
        m_gm->processAudio(buf,deltatime);
        
    }
    std::vector<std::unique_ptr<GrainAudioSource>> m_srcs;
    std::unique_ptr<GrainMixer> m_gm;
    json_t* dataToJson() 
    {
        json_t* resultJ = json_object();
        auto src = dynamic_cast<MultiBufferSource*>(m_srcs[0].get());
        json_t* markerarr = json_array();
        for (int i=0;i<m_markers.size();++i)
        {
            float pos = m_markers[i];
            json_array_append(markerarr,json_real(pos));
        }
        json_object_set(resultJ,"markers",markerarr);
        //json_object_set(resultJ,"scrub_resamplermode",json_integer(m_scrubber->m_resampler_type));
        json_object_set(resultJ,"scrub_volumecompensation",json_integer(m_scrubber->m_compensate_volume));
        return resultJ;
    }
    void dataFromJson(json_t* root) 
    {
        if (!root)
            return;
        //json_t* scrubmodeJ = json_object_get(root,"scrub_resamplermode");
        //if (scrubmodeJ) m_scrubber->m_resampler_type = json_integer_value(scrubmodeJ);
        json_t* scrubmodeJ = json_object_get(root,"scrub_volumecompensation");
        if (scrubmodeJ) m_scrubber->m_compensate_volume = json_integer_value(scrubmodeJ);
        
        json_t* markers = json_object_get(root, "markers");
        
        int nummarkers = json_array_size(markers);
        if (nummarkers==0)
        {
            return;
        }
        m_markers.clear();
        for (int i=0;i<nummarkers;++i)
        {
            auto elem = json_array_get(markers,i);
            float pos = json_number_value(elem);
            m_markers.push_back(pos);
        }
    }
    int m_playmode = 0; // 0 normal, 1 scan mode, 2 scrub
    float m_scanpos = 0.0f;
private:
    
};
