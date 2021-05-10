#include "plugin.hpp"
#include <random>
#include <stb_image.h>
#include <atomic>
#include <array>
#include <functional>
#include <thread> 
#include <mutex>

#include "wdl/resample.h"
#include <chrono>

#include "grain_engine/grain_engine.h"

extern std::shared_ptr<Font> g_font;

struct PanMode
{
    PanMode(const char* d, int nch, int uc) : desc(d), numoutchans(nch), usecolors(uc) {}
    const char* desc = nullptr;
    int numoutchans = 0;
    int usecolors = 0;
};

PanMode g_panmodes[7]=
{
    {"Mono (ignore colors)",1,0},
    {"Stereo (ignore colors, random panning)",2,0},
    {"Stereo (ignore colors, alternate panning)",2,0},
    {"Stereo (pan based on red/yellow/green)",2,1},
    {"Quad (ignore colors, random panning)",4,0},
    {"Quad (ignore colors, alternate panning)",4,0},
    {"Quad (pan in circle based on red/yellow/green)",4,1}
};

const int g_wtsize = 2048;

float g_freq_to_gain_tables[5][5]=
{
    {1.0f,0.0f,0.0f,0.0f,0.0f},
    {1.0f,1.0f,0.66f,0.33f,0.0f},
    {1.0f,1.0f,1.0f,1.0f,1.0f},
    {0.0f,0.33f,0.66f,1.0f,1.0f},
    {0.0f,0.0f,0.0f,0.0f,1.0f}
};

inline float get_gain_curve_value(float morph,float x)
{
    int index_y0=std::floor(morph*4);
    int index_y1=index_y0+1;
    if (index_y1>4)
        index_y1=4;
    float frac_y = (morph*4.0f)-index_y0;
    float morphedtable[5];
    for (int i=0;i<5;++i)
    {
        float r0=g_freq_to_gain_tables[index_y0][i];
        float r1=g_freq_to_gain_tables[index_y1][i];
        float v0 = r0+(r1-r0)*frac_y;
        morphedtable[i]=v0;
    }
    int index_x0 = std::floor(x*4);
    int index_x1 = index_x0+1;
    if (index_x1>4)
        index_x1=4;
    float frac_x = (x*4.0f)-index_x0;
    float r0=morphedtable[index_x0];
    float r1=morphedtable[index_x1];
    float v0 = r0+(r1-r0)*frac_x;

    return v0;
}

template <typename T>
inline T triplemax (T a, T b, T c)                           
{ 
    return a < b ? (b < c ? c : b) : (a < c ? c : a); 
}


inline float harmonics3(float xin)
{
    return 0.5 * std::sin(xin) + 0.25 * std::sin(xin * 2.0) + 0.1 * std::sin(xin * 3);
}

inline float harmonics4(float xin)
{
    return 0.5 * std::sin(xin) + 0.25 * std::sin(xin * 2.0) + 0.1 * std::sin(xin * 3) +
        0.15*std::sin(xin*7);
}

class SIMDImgWaveOscillator
{
public:
    void initialise(std::function<float(float)> f, 
    int tablesize)
    {
        m_tablesize = tablesize;
        m_table.resize(tablesize+1);
        for (int i=0;i<tablesize;++i)
            m_table[i] = f(rescale(i,0,tablesize-1,-g_pi,g_pi));
        m_table[tablesize] = m_table[tablesize-1];
    }
    void setFrequency(simd::float_4 hz)
    {
        m_phaseincrement = m_tablesize*hz*(1.0/m_sr);
        m_freq = hz;
    }
    simd::float_4 getFrequency()
    {
        return m_freq;
    }
    simd::float_4 processSample(float)
    {
        simd::int32_4 index0 = simd::floor(m_phase);
        simd::int32_4 index1 = simd::floor(m_phase)+1;
        simd::float_4 frac = m_phase - simd::float_4(index0);
        simd::float_4 y0(m_table[index0[0]],m_table[index0[1]],m_table[index0[2]],m_table[index0[3]]);
        simd::float_4 y1(m_table[index1[0]],m_table[index1[1]],m_table[index1[2]],m_table[index1[3]]);
        simd::float_4 sample = y0+(y1-y0)*frac;
        
        m_phase+=m_phaseincrement;
        m_phase = (m_phase>=m_tablesize) & m_phase-m_tablesize;
        return sample;
    }
    void prepare(int numchans, float sr)
    {
        m_sr = sr;
        setFrequency(m_freq);
    }
    void reset(float initphase)
    {
        m_phase = initphase;
    }
    void setTable(std::vector<float> tb)
    {
        m_tablesize = tb.size();
        m_table = tb;
    }
private:
    int m_tablesize = 0;
    std::vector<float> m_table;
    simd::float_4 m_phase = 0.0;
    float m_sr = 44100.0f;
    simd::float_4 m_phaseincrement = 0.0f;
    simd::float_4 m_freq = 440.0f;
};

class ImgWaveOscillator
{
public:
    void initialise(std::function<float(float)> f, 
    int tablesize)
    {
        m_tablesize = tablesize;
        m_table.resize(tablesize);
        for (int i=0;i<tablesize;++i)
            m_table[i] = f(rescale(i,0,tablesize-1,-g_pi,g_pi));
    }
    void setFrequency(float hz)
    {
        m_phaseincrement = m_tablesize*hz*(1.0/m_sr);
        m_freq = hz;
    }
    float getFrequency()
    {
        return m_freq;
    }
    float processSample(float)
    {
        /*
        int index = m_phase;
        float sample = m_table[index];
        m_phase+=m_phaseincrement;
        if (m_phase>=m_tablesize)
            m_phase-=m_tablesize;
        */
        int index0 = std::floor(m_phase);
        int index1 = std::floor(m_phase)+1;
        if (index1>=m_tablesize)
            index1 = 0;
        float frac = m_phase-index0;
        float y0 = m_table[index0];
        float y1 = m_table[index1];
        float sample = y0+(y1-y0)*frac;
        m_phase+=m_phaseincrement;
        if (m_phase>=m_tablesize)
            m_phase-=m_tablesize;
        return sample;
    }
    void prepare(int numchans, float sr)
    {
        m_sr = sr;
        setFrequency(m_freq);
    }
    void reset(float initphase)
    {
        m_phase = initphase;
    }
    void setTable(std::vector<float> tb)
    {
        m_tablesize = tb.size();
        m_table = tb;
    }
private:
    int m_tablesize = 0;
    std::vector<float> m_table;
    double m_phase = 0.0;
    float m_sr = 44100.0f;
    float m_phaseincrement = 0.0f;
    float m_freq = 440.0f;
};

class ImgOscillator
{
public:
    float* m_gainCurve = nullptr;
    ImgOscillator()
    {
        for (int i = 0; i < 4; ++i)
            m_pan_coeffs[i] = 0.0f;
        m_osc.initialise([](float x)
            {
                return 0.5 * std::sin(x) + 0.25 * std::sin(x * 2.0) + 0.1 * std::sin(x * 3);

            }, g_wtsize);
    }
    float m_freq = 440.0f;
    void setFrequency(float hz)
    {
        m_osc.setFrequency(hz);
        m_freq = hz;
    }
    void setEnvelopeAmount(float amt)
    {
        a = rescale(amt, 0.0f, 1.0f, 0.9f, 0.9999f);
        b = 1.0 - a;
    }
    void generateBuffer(float* outbuffer, float* outauxbuffer, int nsamples, float pix_mid_gain, float aux_value)
    {
        int gain_index = rescale(pix_mid_gain, 0.0f, 1.0f, 0, 255);
        pix_mid_gain = m_gainCurve[gain_index];
        for (int i=0;i<nsamples;++i)
        {
            float z = (pix_mid_gain * b) + (m_env_state * a);
            if (z < m_cut_th)
                z = 0.0;
            m_env_state = z;
            float pan_z = (aux_value*b)+(m_pan_env_state*a);
            outauxbuffer[i] = pan_z;
            m_pan_env_state = pan_z;
            if (z > 0.00)
            {
                outbuffer[i] = z * m_osc.processSample(0.0f);
            }
            else
                outbuffer[i] = 0.0f;
            }
        
    }
    void generate(float pix_mid_gain, float aux_value)
    {
        int gain_index = rescale(pix_mid_gain, 0.0f, 1.0f, 0, 255);
        pix_mid_gain = m_gainCurve[gain_index];
        float z = (pix_mid_gain * b) + (m_env_state * a);
        if (z < m_cut_th)
            z = 0.0;
        m_env_state = z;
        float pan_z = (aux_value*b)+(m_pan_env_state*a);
        outAuxValue = pan_z;
        m_pan_env_state = pan_z;
        if (z > 0.00)
        {
            outSample = z * m_osc.processSample(0.0f);
        }
        else
            outSample = 0.0f;

    }
    ImgWaveOscillator m_osc;
    float outSample = 0.0f;
    float outAuxValue = 0.0f;
    //private:
    
    
    float m_env_state = 0.0f;
    float m_pan_env_state = 0.0f;
    float m_pan_coeffs[4];
    float m_cut_th = 0.0f;
    float a = 0.998;
    float b = 1.0 - a;
};

class OscillatorBuilder;

class ImgSynth : public GrainAudioSource
{
public:
    std::mt19937 m_rng{ 99937 };
    std::list<std::string> m_scala_scales;
    ImgSynth()
    {
        
        m_pixel_to_gain_table.resize(256);
        m_oscillators.resize(1024);
        m_freq_gain_table.resize(1024);
        currentFrequencies.resize(1024);
        m_sinTable.resize(512);
        m_cosTable.resize(512);
        for (int i=0;i<(int)m_sinTable.size();++i)
        {
            m_sinTable[i] = std::sin(2*g_pi/m_sinTable.size()*i);
            m_cosTable[i] = std::cos(2*g_pi/m_sinTable.size()*i);
        }
    }
    stbi_uc* m_img_data = nullptr;
    int m_img_w = 0;
    int m_img_h = 0;
    std::string currentScalaFile;
    void setImage(stbi_uc* data, int w, int h)
    {
        m_img_data = data;
        m_img_w = w;
        m_img_h = h;
        float thefundamental = rack::dsp::FREQ_C4 * pow(2.0, 1.0 / 12 * m_fundamental);
        float f = thefundamental;
        
        std::vector<float> scale;
        if (m_frequencyMapping>=3)
        {
            auto it = m_scala_scales.begin();
            std::advance(it,m_frequencyMapping-3);
            std::string filename = *it;
            scale = loadScala(filename,true,m_minPitch,m_maxPitch);
            currentScalaFile = filename;
            //for (auto& e : scale)
            //    std::cout << e << " , ";
            std::cout << "\n";
            if (scale.size()<2 && m_frequencyMapping == 3)
            {
                m_frequencyMapping = 0;
                currentScalaFile ="Failed to load .scl file";
            }
        }
        if (m_frequencyMapping == 0 || m_frequencyMapping>=3)
        {
            minFrequency = 32.0 * pow(2.0, 1.0 / 12 * m_minPitch);
            maxFrequency = 32.0 * pow(2.0, 1.0 / 12 * m_maxPitch);
        }
        else if (m_frequencyMapping == 1)
        {
            minFrequency = 32.0 * pow(2.0, 1.0 / 12 * m_minPitch);
            maxFrequency = 32.0 * pow(2.0, 1.0 / 12 * m_maxPitch);
        }
        else if (m_frequencyMapping == 2)
        {
            minFrequency = thefundamental;
            maxFrequency = thefundamental*64;
        }
        for (int i = 0; i < h; ++i)
        {
            if (m_frequencyMapping == 0)
            {
                float pitch = rescale(i, 0, h, m_maxPitch, m_minPitch);
                float frequency = 32.0 * pow(2.0, 1.0 / 12 * pitch);
                m_oscillators[i].m_osc.setFrequency(frequency);
            }
            if (m_frequencyMapping == 1)
            {
                float frequency = rescale(i, 0, h, maxFrequency, minFrequency);
                m_oscillators[i].m_osc.setFrequency(frequency);
            }
            if (m_frequencyMapping == 2)
            {
                int harmonic = rescale(i, 0, h, 64.0f, 1.0f);
                f = thefundamental*harmonic;
                std::uniform_real_distribution<float> detunedist(-1.0f,1.0f);
                if (f>127.0f)
                    f+=detunedist(m_rng);
                m_oscillators[i].m_osc.setFrequency(f);
            }
            if (m_frequencyMapping >= 3)
            {
                float pitch = rescale(i, 0, (h-1.0f), m_maxPitch, m_minPitch);
                pitch = quantize_to_grid(pitch,scale,m_scala_quan_amount);
                float frequency = 32.0 * pow(2.0, 1.0 / 12 * pitch);
                m_oscillators[i].m_osc.setFrequency(frequency);
            }
            currentFrequencies[i] = m_oscillators[i].m_osc.getFrequency();
            float normf = rescale(i,0,h,1.0f,0.0f);
            float resp_gain = get_gain_curve_value(m_freq_response_curve,normf);
            m_freq_gain_table[i] = resp_gain;
            
        }
        
    }
    void render(float outdur, float sr, OscillatorBuilder& oscbuilder);
    

    float percentReady()
    {
        return m_percent_ready;
    }
    
    
    float m_maxGain = 0.0f;
    double m_elapsedTime = 0.0f;
    std::atomic<bool> m_shouldCancel{ false };
    
    
    
    int m_stepsize = 64;
    
    
    void setFrequencyMapping(int m)
    {
        if (m!=m_frequencyMapping)
        {
            m_frequencyMapping = m;
            startDirtyCountdown();
        }
    }
    void setFrequencyResponseCurve(float x)
    {
        if (x!=m_freq_response_curve)
        {
            m_freq_response_curve = x;
            startDirtyCountdown();
        }
    }
    void setEnvelopeShape(float x)
    {
        if (x!=m_envAmount)
        {
            m_envAmount = x;
            startDirtyCountdown();
        }
    }
    void setWaveFormType(int x)
    {
        if (x!=m_waveFormType)
        {
            m_waveFormType = x;
            startDirtyCountdown();
        }
    }
    int getWaveFormType() { return m_waveFormType; }
    int m_numOutputSamples = 0;
    int getNumOutputSamples()
    {
        return m_numOutputSamples;
    }

    void setHarmonicsFundamental(float semitones)
    {
        if (semitones!=m_fundamental)
        {
            m_fundamental = semitones;
            startDirtyCountdown();
        }
    }

    

    void setPixelGainCurve(float x)
    {
        if (x!=m_pixel_to_gain_curve)
        {
            m_pixel_to_gain_curve = x;
            startDirtyCountdown();
        }
    }

    void setOutputChannelsMode(int m)
    {
        if (m!=m_outputChansMode)
        {
            m_outputChansMode = m;
            startDirtyCountdown();
        }
    }
    
    int getNumOutputChannels() 
    { 
        return g_panmodes[m_outputChansMode].numoutchans;
    }

    void setScalaTuningAmount(float x)
    {
        if (x!=m_scala_quan_amount)
        {
            m_scala_quan_amount = x;
            startDirtyCountdown();
        }
    }

    void setPitchRange(float a, float b)
    {
        if (a!=m_minPitch || b!=m_maxPitch)
        {
            if (a>b)
                std::swap(a,b);
            m_minPitch = a;
            m_maxPitch = b;
            startDirtyCountdown();
        }
    }

    void startDirtyCountdown()
    {
        m_isDirty = true;
        m_lastSetDirty = std::chrono::steady_clock::now();
    }
    float getDirtyElapsedTime()
    {
        if (m_isDirty==false)
            return 0.0f;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSetDirty).count();
        return elapsed/1000.0f;
    }
    // keep false while resizing the buffer, the playback code
    // checks that to skip rendering samples
    std::atomic<bool> m_BufferReady{false};

    std::vector<float> currentFrequencies;
    float minFrequency = 0.0f;
    float maxFrequency = 1.0f;
    float getBufferSample(int index)
    {
        if (m_BufferReady==false)
            return 0.0f;
        if (index>=0 && index<(int)m_renderBuf.size())
            return m_renderBuf[index];
        return 0.0f;
    }
    void putIntoBuffer(float* dest, int numFrames, int numChannels, int startFrame) override
    {
        int outchanstouse = getNumOutputChannels();
        if (m_BufferReady == false || outchanstouse == 0 || m_numOutputSamples == 0)
        {
            for (int i=0;i<numFrames*outchanstouse;++i)
                dest[i]=0.0;
            return;
        }
        //return;
        int maxFrame = m_numOutputSamples;
        for (int i=0;i<numFrames;++i)
        {
            int frameIndex = startFrame+i;
            if (frameIndex<maxFrame)
            {
                for (int j=0;j<outchanstouse;++j)
                {
                    dest[i*outchanstouse+j] = m_renderBuf[frameIndex*outchanstouse+j];
                }
            } else
            {
                for (int j=0;j<outchanstouse;++j)
                {
                    dest[i*outchanstouse+j] = 0.0;
                }
            }
        }    
    }
private:
    std::vector<float> m_renderBuf;
    std::chrono::steady_clock::time_point m_lastSetDirty;
    bool m_isDirty = false;
    int m_frequencyMapping = 0;
    std::vector<ImgOscillator> m_oscillators;
    std::vector<float> m_freq_gain_table;
    std::vector<float> m_pixel_to_gain_table;
    std::vector<float> m_sinTable;
    std::vector<float> m_cosTable;
    std::atomic<float> m_percent_ready{ 0.0 };
    float m_freq_response_curve = 0.5f;
    float m_envAmount = 0.95f;
    int m_waveFormType = 0;
    
    float m_fundamental = -24.0f; // semitones below middle C!
    
    int m_outputChansMode = 1;
    float m_scala_quan_amount = 0.99f;
    float m_pixel_to_gain_curve = 1.0f;
    float m_minPitch = 0.0f;
    float m_maxPitch = 102.0f;
};

class OscillatorBuilder
{
public:
    OscillatorBuilder(int numharmonics)
    {
        m_table.resize(m_tablesize);
        m_harmonics.resize(numharmonics);
        m_harmonics[0] = 1.0f;
        m_harmonics[1] = 0.5f;
        m_harmonics[2] = 0.25f;
        m_harmonics[4] = 0.125f;
        m_harmonics[13] = 0.5f;
        m_osc.prepare(1,m_samplerate);
        updateOscillator();
    }
    void updateOscillator()
    {
        for (int i=0;i<m_tablesize;++i)
        {
            float sum = 0.0f;
            for (int j=0;j<(int)m_harmonics.size();++j)
            {
                sum+=m_harmonics[j]*std::sin(2*g_pi/m_tablesize*i*(j+1));
            }
            m_table[i]=sum;
        }
        auto it = std::max_element(m_table.begin(),m_table.end());
        float normscaler = 1.0f / *it;
        for (int i=0;i<m_tablesize;++i)
            m_table[i]*=normscaler;
        m_generating = true;
        m_osc.setTable(m_table);
        m_generating = false;
    }
    float process()
    {
        if (m_generating)
            return 0.0f;
        return m_osc.processSample(0.0f);
    }
    void setFrequency(float hz)
    {
        m_osc.setFrequency(hz);
    }
    float getHarmonic(int index)
    {
        if (index>=0 && index<(int)m_harmonics.size())
            return m_harmonics[index];
        return 0.0f;
    }
    void setHarmonic(int index, float v)
    {
        if (index>=0 && index<(int)m_harmonics.size())
        {
            m_harmonics[index] = v;
            m_dirty = true;
        }
    }
    int getNumHarmonics()
    {
        return m_harmonics.size();
    }
    std::vector<float> getTable()
    {
        return m_table;
    }
    std::vector<float> getTableForFrequency(int size, float hz, float sr)
    {
        std::vector<float> result(size);
        float th = rack::dsp::dbToAmplitude(-60.0);
        for (int i=0;i<size;++i)
        {
            float sum = 0.0f;
            for (int j=0;j<(int)m_harmonics.size();++j)
            {
                float checkfreq = hz*(j+1);
                if (checkfreq < sr/2.0 && m_harmonics[j]>th)
                {
                    double phase = rescale(i,0,size-1,-g_pi,g_pi);
                    sum+=m_harmonics[j]*std::sin(phase*(j+1));
                    //sum+=m_harmonics[j]*std::sin(2*3.141592653/(size-1)*i*(j+1));
                }
                    
            }
            result[i]=sum;
        }
        auto it = std::max_element(result.begin(),result.end());
        float normscaler = 0.0f;
        if (*it>0.0)
            normscaler = 1.0f / *it;
        for (int i=0;i<size;++i)
            result[i]*=normscaler;
        return result;
    }
    bool m_dirty = true;
private:
    std::vector<float> m_harmonics;
    std::vector<float> m_table;
    ImgWaveOscillator m_osc;
    int m_tablesize = g_wtsize;
    float m_samplerate = 44100;
    std::atomic<bool> m_generating{false};
};

void  ImgSynth::render(float outdur, float sr, OscillatorBuilder& oscBuilder)
    {
        m_numOutputSamples = 0;
        m_isDirty = false;
        m_shouldCancel = false;
        m_elapsedTime = 0.0;
        std::uniform_real_distribution<float> dist(0.0, g_pi);
        auto t0 = std::chrono::steady_clock::now();
        const float cut_th = rack::dsp::dbToAmplitude(-72.0f);
        m_maxGain = 1.0f;
        m_percent_ready = 0.0f;
        m_BufferReady = false;
        int ochanstouse = g_panmodes[m_outputChansMode].numoutchans;
        m_renderBuf.resize(ochanstouse * ((1.0 + outdur) * sr));
        //int auxChanIdx = m_numOutChans;
        m_BufferReady = true;
        for (int i = 0; i < 256; ++i)
        {
            m_pixel_to_gain_table[i] = std::pow(1.0 / 256 * i,m_pixel_to_gain_curve);
        }
        
        std::uniform_real_distribution<float> pandist(0.0, g_pi / 2.0f);
        for (int i = 0; i < (int)m_oscillators.size(); ++i)
        {
            m_oscillators[i].m_osc.prepare(1,sr);
            m_oscillators[i].m_osc.reset(dist(m_rng));
            m_oscillators[i].m_env_state = 0.0f;
            m_oscillators[i].m_cut_th = cut_th;
            m_oscillators[i].setEnvelopeAmount(m_envAmount);
            m_oscillators[i].m_gainCurve = m_pixel_to_gain_table.data();
            if (m_waveFormType == 0)
                m_oscillators[i].m_osc.initialise([](float xin){ return std::sin(xin); },g_wtsize);
            else if (m_waveFormType == 1)
                m_oscillators[i].m_osc.initialise([](float xin)
                                                { return harmonics3(xin);},g_wtsize);
            else if (m_waveFormType == 2)
                m_oscillators[i].m_osc.initialise([](float xin)
                                                  { return harmonics4(xin);},g_wtsize);
            else if (m_waveFormType == 3)
            {
                if (oscBuilder.m_dirty)
                {
                    float oschz = m_oscillators[i].m_freq;
                    m_oscillators[i].m_osc.setTable(oscBuilder.getTableForFrequency(g_wtsize,oschz,sr));
                }
                
            }
            if (m_outputChansMode == 0)
            {
                m_oscillators[i].m_pan_coeffs[0] = 0.71f;
                m_oscillators[i].m_pan_coeffs[1] = 0.71f;
            }
            if (m_outputChansMode == 1)
            {
                float panpos = pandist(m_rng);
                m_oscillators[i].m_pan_coeffs[0] = std::cos(panpos);
                m_oscillators[i].m_pan_coeffs[1] = std::sin(panpos);
            }
            if (m_outputChansMode == 4)
            {
                float angle = pandist(m_rng) * 2.0f; // position along circle
                float panposx = rescale(std::cos(angle), -1.0f, 1.0, 0.0f, g_pi);
                float panposy = rescale(std::sin(angle), -1.0f, 1.0, 0.0f, g_pi);
                m_oscillators[i].m_pan_coeffs[0] = std::cos(panposx);
                m_oscillators[i].m_pan_coeffs[1] = std::sin(panposx);
                m_oscillators[i].m_pan_coeffs[2] = std::cos(panposy);
                m_oscillators[i].m_pan_coeffs[3] = std::sin(panposy);
            }
            
            if (m_outputChansMode == 2 || m_outputChansMode == 5)
            {
                int outspeaker = i % ochanstouse;
                for (int j = 0; j < ochanstouse; ++j)
                {
                    if (j == outspeaker)
                        m_oscillators[i].m_pan_coeffs[j] = 1.0f;
                    else m_oscillators[i].m_pan_coeffs[j] = 0.0f;
                }

            }
            
        }
        int imgw = m_img_w;
        int imgh = m_img_h;
        int outdursamples = sr * outdur;
        //m_renderBuf.clear();
        bool usecolors = g_panmodes[m_outputChansMode].usecolors;
        std::vector<float> mainProcBuf(m_stepsize);
        std::vector<float> panProcBuf(m_stepsize);
        for (int x = 0; x < outdursamples; x += m_stepsize)
        {
            if (m_shouldCancel)
                break;
            m_percent_ready = 1.0 / outdursamples * x;
            for (int i = 0; i < m_stepsize; ++i)
            {
                for (int chan=0;chan<ochanstouse;++chan)
                {
                    m_renderBuf[(x+i)*ochanstouse+chan]=0.0f;    
                }
            }
            for (int y = 0; y < imgh; ++y)
            {
                int xcor = rescale(x, 0, outdursamples, 0, imgw);
                if (xcor>=imgw)
                    xcor = imgw-1;
                if (xcor<0)
                    xcor = 0;
                const stbi_uc *p = m_img_data + (4 * (y * imgw + xcor));
                unsigned char r = p[0];
                unsigned char g = p[1];
                unsigned char b = p[2];
                //unsigned char a = p[3];
                float pix_mid_gain = (float)triplemax(r,g,b)/255.0f;
                //float send_gain = (float)g/255.0f-((float)(r+b)/512.0f);
                //send_gain = clamp(send_gain,0.0f,1.0f);
                float aux_param = (-r/255.0)+(g/255.0);
                aux_param = (aux_param+1.0f)*0.5f;
                
                /*
                if (m_numOutChans == 4)
                {
                    float panx = 0.5f + 0.5f * std::cos(2*3.141592653*aux_param);
                    float pany = 0.5f + 0.5f * std::sin(2*3.141592653*aux_param);
                    pangains[0] = 1.0f - panx;
                    pangains[1] = panx;
                    pangains[2] = pany;
                    pangains[3] = 1.0f - pany;
                }
                */
                float pangains[4]={0.0f,0.0f,0.0f,0.0f};
                if (usecolors==false)
                {
                    for (int i=0;i<ochanstouse;++i)
                        pangains[i]=m_oscillators[y].m_pan_coeffs[i];
                }
                m_oscillators[y].generateBuffer(mainProcBuf.data(),panProcBuf.data(), m_stepsize, pix_mid_gain,aux_param);
                for (int i = 0; i < m_stepsize; ++i)
                {
                    //m_oscillators[y].generate(pix_mid_gain, aux_param);
                    //float sample = m_oscillators[y].outSample;
                    float sample = mainProcBuf[i];
                    if (fabs(sample) > 0.0f)
                    {
                        //float auxval = m_oscillators[y].outAuxValue;
                        float auxval = panProcBuf[i];
                        if (usecolors)
                        {
                            if (ochanstouse == 2)
                            {
                                pangains[0] = auxval;
                                pangains[1] = 1.0-auxval;
                                pangains[2] = 0.0f;
                                pangains[3] = 0.0f;
                            }
                            else if (ochanstouse == 1)
                            {
                                pangains[0] = 1.0f;
                            }
                            else if (ochanstouse == 4)
                            {
                                int trigindex = aux_param*511;
                                if (trigindex<0)
                                    trigindex = 0;
                                if (trigindex>511)
                                    trigindex = 511;
                                float panx = 0.5f+0.5f*m_cosTable[trigindex];
                                float pany = 0.5f+0.5f*m_sinTable[trigindex];
                                pangains[0] = 1.0f - panx;
                                pangains[1] = panx;
                                pangains[2] = pany;
                                pangains[3] = 1.0f - pany;
                            }
                        }
                        
                        float resp_gain = m_freq_gain_table[y];
                        
                        for (int chan = 0; chan < ochanstouse; ++chan)
                        {
                            int outbufindex = (x + i)*ochanstouse+chan;
                            float previous = m_renderBuf[outbufindex];
                            //previous += sample * 0.1f * resp_gain * m_oscillators[y].m_pan_coeffs[chan];
                            previous += sample * 0.1f * resp_gain * pangains[chan];
                            m_renderBuf[outbufindex] = previous;
                        }
                        //int outbufauxindex = (x + i)*outchanstouse+auxChanIdx;
                        //m_renderBuf[outbufauxindex]+= sample*0.1f*resp_gain*send_gain;
                    }
                }

            }

        }
        if (!m_shouldCancel)
        {
            auto it = std::max_element(m_renderBuf.begin(),m_renderBuf.end());
            m_maxGain = *it; 
            auto t1 = std::chrono::steady_clock::now();
            m_elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()/1000.0;
        }
        m_numOutputSamples = outdursamples;
        m_percent_ready = 1.0;
    }

class XImageSynth : public rack::Module
{
public:
    enum Inputs
    {
        IN_PITCH_CV,
        IN_RESET,
        IN_LOOPSTART_CV,
        IN_LOOPLEN_CV,
        IN_GRAINPLAYRATE_CV,
        LAST_INPUT
    };
    enum Outputs
    {
        OUT_AUDIO,
        OUT_LOOP_SWITCH,
        OUT_LOOP_PHASE,
        LAST_OUTPUT
    };
    enum Parameters
    {
        PAR_RELOAD_IMAGE,
        PAR_DURATION,
        PAR_PITCH,
        PAR_FREQMAPPING,
        PAR_WAVEFORMTYPE,
        PAR_PRESET_IMAGE,
        PAR_LOOP_START,
        PAR_LOOP_LEN,
        PAR_FREQUENCY_BALANCE,
        PAR_HARMONICS_FUNDAMENTAL,
        PAR_PAN_MODE,
        PAR_NUMOUTCHANS,
        PAR_DESIGNER_ACTIVE,
        PAR_DESIGNER_VOLUME,
        PAR_ENVELOPE_SHAPE,
        PAR_SCALA_TUNING_AMOUNT,
        PAR_MINPITCH,
        PAR_MAXPITCH,
        PAR_LOOPMODE,
        PAR_GRAIN_PLAYSPEED,
        PAR_GRAIN_SIZE,
        PAR_GRAIN_RANDOM,
        PAR_PLAYBACKMODE,
        PAR_OUTLIMITMODE,
        PAR_LAST
    };
    int m_comp = 0;
    std::list<std::string> presetImages;
    std::vector<stbi_uc> m_backupdata; 
    dsp::BooleanTrigger reloadTrigger;
    std::atomic<bool> m_renderingImage;
    float loopstart = 0.0f;
    float looplen = 1.0f;
    
    OscillatorBuilder m_oscBuilder{32};
    std::list<std::string> m_scala_scales;
    dsp::DoubleRingBuffer<dsp::Frame<5>, 256> outputBuffer;
    std::vector<float> srcOutBuffer;
    OnePoleFilter gainSmoother;
    XImageSynth()
    {
        srcOutBuffer.resize(16*64);
        m_scala_scales = rack::system::getEntries(asset::plugin(pluginInstance, "res/scala_scales"));
        m_syn.m_scala_scales = m_scala_scales;
        m_renderingImage = false;
        presetImages = rack::system::getEntries(asset::plugin(pluginInstance, "res/image_synth_images"));
        config(PAR_LAST,LAST_INPUT,LAST_OUTPUT,0);
        configParam(PAR_RELOAD_IMAGE,0,1,1,"Reload image");
        configParam(PAR_DURATION,0.5,60,5.0,"Image duration");
        configParam(PAR_PITCH,-24,24,0.0,"Playback pitch");
        configParam(PAR_FREQMAPPING,0,2+(m_scala_scales.size()),0.0,"Frequency mapping type");
        configParam(PAR_WAVEFORMTYPE,0,3,0.0,"Oscillator type");
        configParam(PAR_PRESET_IMAGE,0,presetImages.size()-1,0.0,"Preset image");
        configParam(PAR_LOOP_START,0.0,0.99,0.0,"Loop start");
        configParam(PAR_LOOP_LEN,0.00,1.00,1.0,"Loop length");
        configParam(PAR_FREQUENCY_BALANCE,0.00,1.00,0.25,"Frequency balance");
        configParam(PAR_HARMONICS_FUNDAMENTAL,-72.0,0.00,-24.00,"Harmonics fundamental");
        configParam(PAR_PAN_MODE,0.0,6.0,1.00,"Frequency panning mode");
        configParam(PAR_NUMOUTCHANS,0.0,6.0,0.00,"Output channels configuration");
        configParam(PAR_DESIGNER_ACTIVE,0,1,0,"Edit oscillator waveform");
        configParam(PAR_DESIGNER_VOLUME,-24.0,3.0,-12.0,"Oscillator editor volume");
        configParam(PAR_ENVELOPE_SHAPE,0.0,1.0,0.95,"Envelope shape");
        configParam(PAR_SCALA_TUNING_AMOUNT,0.0,1.0,0.99,"Scala tuning amount");
        configParam(PAR_MINPITCH,0.0,102.0,0.0,"Minimum pitch");
        configParam(PAR_MAXPITCH,0.0,102.0,90.0,"Maximum pitch");
        configParam(PAR_LOOPMODE,0,1,0,"Looping mode");
        configParam(PAR_GRAIN_PLAYSPEED,-2.0,2.0,1.0,"Play rate");
        configParam(PAR_GRAIN_SIZE,0.005,0.25,0.05,"Grain size");
        configParam(PAR_GRAIN_RANDOM,0.0,0.1,0.05,"Grain random");
        configParam(PAR_PLAYBACKMODE,0,1,0,"Playback mode");
        configParam(PAR_OUTLIMITMODE,0,3,0,"Output volume limit mode");
        gainSmoother.setAmount(0.9999);
    }
    void onAdd() override
    {
        //reloadImage();
    }
    int renderCount = 0;
    int m_currentPresetImage = 0;
    
    void reloadImage()
    {
        ++renderCount;
        if (m_renderingImage==true)
            return;
        auto task=[this]
        {
        
        
        auto imagetofree = m_img_data;
        m_mtx.lock();
        m_img_data = nullptr;
        m_img_w = 0;
        m_img_h = 0;
        m_mtx.unlock();
        int imagetoload = params[PAR_PRESET_IMAGE].getValue();
        auto it = presetImages.begin();
        std::advance(it,imagetoload);
        std::string filename = *it;
        int comp = 0;
        int temp_w = 0;
        int temp_h = 0;
        auto tempdata = stbi_load(filename.c_str(),&temp_w,&temp_h,&comp,4);

        m_playpos = 0.0f;
        //m_bufferplaypos = 0;
        
        int outconf = params[PAR_NUMOUTCHANS].getValue();
        
        m_syn.setOutputChannelsMode(outconf);
        
        m_mtx.lock();
        stbi_image_free(imagetofree);
        
        m_img_data = tempdata;
        m_img_w = temp_w;
        m_img_h = temp_h;
        m_mtx.unlock();
        m_img_data_dirty = true;
        
        m_syn.setFrequencyMapping(params[PAR_FREQMAPPING].getValue());
        m_syn.setFrequencyResponseCurve(params[PAR_FREQUENCY_BALANCE].getValue());
        m_syn.setHarmonicsFundamental(params[PAR_HARMONICS_FUNDAMENTAL].getValue());
        int wtype = params[PAR_WAVEFORMTYPE].getValue();
        if (m_syn.getWaveFormType()!=3 && wtype == 3)
            m_oscBuilder.m_dirty = true;
        m_syn.setWaveFormType(wtype);
        m_syn.setEnvelopeShape(params[PAR_ENVELOPE_SHAPE].getValue());
        m_syn.setImage(m_img_data ,m_img_w,m_img_h);
        m_out_dur = params[PAR_DURATION].getValue();
        m_syn.render(m_out_dur,44100,m_oscBuilder);
        m_oscBuilder.m_dirty = false;
        m_renderingImage = false;
        };
        m_renderingImage = true;
        std::thread th(task);
        th.detach();
    }
    int m_timerCount = 0;
    float m_checkOutputDur = 0.0f;
    void onTimer()
    {
        ++m_timerCount;
        if (!m_renderingImage)
        {
            m_syn.setFrequencyResponseCurve(params[PAR_FREQUENCY_BALANCE].getValue());
            m_syn.setFrequencyMapping(params[PAR_FREQMAPPING].getValue());
            m_syn.setEnvelopeShape(params[PAR_ENVELOPE_SHAPE].getValue());
            m_syn.setHarmonicsFundamental(params[PAR_HARMONICS_FUNDAMENTAL].getValue());
            
            m_syn.setScalaTuningAmount(params[PAR_SCALA_TUNING_AMOUNT].getValue());
            m_syn.setPitchRange(params[PAR_MINPITCH].getValue(),params[PAR_MAXPITCH].getValue());
            int outconf = params[PAR_NUMOUTCHANS].getValue();
            
            m_syn.setOutputChannelsMode(outconf);
            int wtype = params[PAR_WAVEFORMTYPE].getValue();
            if (m_syn.getWaveFormType()!=3 && wtype == 3)
                m_oscBuilder.m_dirty = true;
            m_syn.setWaveFormType(wtype);
            int imagetoload = params[PAR_PRESET_IMAGE].getValue();
            if (imagetoload!=m_currentPresetImage)
            {
                m_syn.startDirtyCountdown();
                m_currentPresetImage = imagetoload;
            }
            if (m_checkOutputDur!=params[PAR_DURATION].getValue())
            {
                m_checkOutputDur = params[PAR_DURATION].getValue();
                m_syn.startDirtyCountdown();
            }
            if (m_syn.getDirtyElapsedTime()>0.5)
            {
                reloadImage();
            }
        }
        
    }
    bool loopTrigger = false;
    int loopDir = 1; // forward
    int loopMode = 1; // pingpong
    bool granularActive = true;
    void process(const ProcessArgs& args) override
    {
        int ochans = m_syn.getNumOutputChannels();
        if (ochans>1)
            outputs[OUT_AUDIO].setChannels(ochans);
        else 
            outputs[OUT_AUDIO].setChannels(2);
        granularActive = (int)params[PAR_PLAYBACKMODE].getValue() == 1;
        if (granularActive)
        {
            float grain1out[4];
            
            memset(grain1out,0,4*sizeof(float));
            
            float pspeed = params[PAR_GRAIN_PLAYSPEED].getValue();
            pspeed += rescale(inputs[IN_GRAINPLAYRATE_CV].getVoltage(),-5.0f,5.0f,-2.0f,2.0f);
            pspeed = clamp(pspeed,-2.0,2.0);
            float pitch = params[PAR_PITCH].getValue();
            float gsize = params[PAR_GRAIN_SIZE].getValue();
            float grnd = params[PAR_GRAIN_RANDOM].getValue();
            pitch += inputs[IN_PITCH_CV].getVoltage()*12.0f;
            pitch = clamp(pitch,-36.0,36.0);

            loopstart = params[PAR_LOOP_START].getValue();
            loopstart += inputs[IN_LOOPSTART_CV].getVoltage()/5.0f;
            loopstart = clamp(loopstart,0.0f,1.0f);
            
            looplen = params[PAR_LOOP_LEN].getValue();
            looplen += inputs[IN_LOOPLEN_CV].getVoltage()/5.0f;
            looplen = clamp(looplen,0.0f,1.0f);
            looplen = std::pow(looplen,2.0f);
            m_grainsmixer.m_inputdur = m_out_dur*args.sampleRate;
            m_grainsmixer.m_loopstart = loopstart;
            m_grainsmixer.m_looplen = looplen;
            m_grainsmixer.m_pitch = pitch;
            m_grainsmixer.m_sourcePlaySpeed = pspeed;
            m_grainsmixer.m_posrandamt = grnd;
            m_grainsmixer.setDensity(gsize);
            if (rewindTrigger.process(inputs[IN_RESET].getVoltage()))
                m_grainsmixer.m_srcpos = 0.0f;
            m_grainsmixer.processAudio(grain1out);
            outputs[OUT_AUDIO].setVoltage(grain1out[0]*5.0f,0);
            outputs[OUT_AUDIO].setVoltage(grain1out[1]*5.0f,1);
            m_playpos = m_grainsmixer.getSourcePlayPosition()/args.sampleRate;
            return;
        }
        
        
        if (outputBuffer.empty())
        {
            const int blocksize = 32;
        
        float pitch = params[PAR_PITCH].getValue();
        pitch += inputs[IN_PITCH_CV].getVoltage()*12.0f;
        pitch = clamp(pitch,-36.0,36.0);
        m_src.SetRates(44100 ,44100/pow(2.0,1.0/12*pitch));
        if (params[PAR_DESIGNER_ACTIVE].getValue()>0.5)
        {
            float preview_freq = rack::dsp::FREQ_C4 * pow(2.0, 1.0 / 12 * pitch);
            m_oscBuilder.setFrequency(preview_freq);
            float preview_sample = m_oscBuilder.process();
            float preview_volume = params[PAR_DESIGNER_VOLUME].getValue();
            preview_sample *= rack::dsp::dbToAmplitude(preview_volume);
            outputs[OUT_AUDIO].setVoltage(preview_sample,0);
            outputs[OUT_AUDIO].setVoltage(preview_sample,1);
            return;
        }
        loopMode = params[PAR_LOOPMODE].getValue();
        if (loopMode==0)
            loopDir = 1;
        int outlensamps = m_out_dur*args.sampleRate;
        loopstart = params[PAR_LOOP_START].getValue();
        loopstart += inputs[IN_LOOPSTART_CV].getVoltage()/5.0f;
        loopstart = clamp(loopstart,0.0f,1.0f);
        int loopstartsamps = outlensamps*loopstart;
        looplen = params[PAR_LOOP_LEN].getValue();
        looplen += inputs[IN_LOOPLEN_CV].getVoltage()/5.0f;
        looplen = clamp(looplen,0.0f,1.0f);
        looplen = std::pow(looplen,2.0f);
        int looplensamps = outlensamps*looplen;
        if (looplensamps<256) looplensamps = 256;
        int loopendsampls = loopstartsamps+looplensamps;
        if (loopendsampls>=outlensamps)
            loopendsampls = outlensamps-1;
        int xfadelensamples = 128;
        int ppfadelensamples = 128;
        if (m_bufferplaypos<loopstartsamps)
            m_bufferplaypos = loopstartsamps;
        if (m_bufferplaypos>loopendsampls && loopMode == 1)
            m_bufferplaypos = loopendsampls-1;
        if (rewindTrigger.process(inputs[IN_RESET].getVoltage()))
            m_bufferplaypos = loopstartsamps;
        if (m_bufferplaypos>=m_out_dur*args.sampleRate)
            m_bufferplaypos = loopstartsamps;
        float loop_phase = rescale(m_bufferplaypos,loopstartsamps,loopendsampls,0.0f,10.0f);
        outputs[OUT_LOOP_PHASE].setVoltage(loop_phase);
        float* rsbuf = nullptr;
        int wanted = m_src.ResamplePrepare(blocksize,ochans,&rsbuf);
        for (int i=0;i<wanted;++i)
        {
            float gain_a = 1.0f;
            float gain_b = 0.0f;
            if (m_bufferplaypos>=loopendsampls-xfadelensamples && loopMode == 0)
            {
                gain_a = rescale(m_bufferplaypos,loopendsampls-xfadelensamples,loopendsampls,1.0f,0.0f);
                gain_b = 1.0-gain_a;
            }
            if (loopMode == 1) 
            {
                if (m_bufferplaypos>=loopstartsamps && m_bufferplaypos<loopstartsamps+ppfadelensamples)
                    gain_a = rescale(m_bufferplaypos,loopstartsamps,loopstartsamps+ppfadelensamples,0.0f,1.0f);
                if (m_bufferplaypos>=loopendsampls-ppfadelensamples)
                    gain_a = rescale(m_bufferplaypos,loopendsampls-ppfadelensamples,loopendsampls,1.0f,0.0f);
            }

            int xfadepos = m_bufferplaypos-looplensamps;
            if (xfadepos<0) xfadepos = 0;
            
            for (int j=0;j<ochans;++j)
            {
                rsbuf[i*ochans+j] = gain_a * m_syn.getBufferSample(m_bufferplaypos*ochans+j);
                if (gain_b>0.0f)
                    rsbuf[i*ochans+j] += gain_b * m_syn.getBufferSample(xfadepos*ochans+j);
            }
            m_bufferplaypos+=loopDir;
            if (m_bufferplaypos>=loopendsampls || m_bufferplaypos<loopstartsamps)
            {
                if (loopDir == 1 && loopMode == 0)
                {
                    m_bufferplaypos = loopstartsamps;
                }
                if (loopDir == 1 && loopMode == 1)
                {
                    --m_bufferplaypos;
                    
                    loopDir = -1;
                } else if (loopDir == -1 && loopMode == 1)
                {
                    ++m_bufferplaypos;
                    
                    loopDir = 1;
                }
                
                loopStartPulse.trigger();
            }
            
            
        }
        
        m_src.ResampleOut(srcOutBuffer.data(),wanted,blocksize,ochans);
        for (int i=0;i<blocksize;++i)
        {
            auto frame = outputBuffer.endData();
            for (int j=0;j<ochans;++j)
            {
                frame->samples[j] = srcOutBuffer[i*ochans+j];
            }
            loopTrigger = loopStartPulse.process(args.sampleTime);
            if (loopTrigger)
                frame->samples[4] = 10.0f;
            else
                frame->samples[4] = 0.0f;
            outputBuffer.endIncr(1);
        }
        }
        int olimmode = params[PAR_OUTLIMITMODE].getValue();
        float adjustgain = 1.0f;
        if (olimmode == 1 && m_syn.m_maxGain>0.0f)
            adjustgain = 1.0f/m_syn.m_maxGain;
        adjustgain = gainSmoother.process(adjustgain);
        if (!outputBuffer.empty())
        {
            if (ochans>1)
            {
                
                auto outFrame = outputBuffer.shift();
                for (int i=0;i<ochans;++i)
                {
                    float outsample = outFrame.samples[i]*adjustgain;
                    if (olimmode == 2)
                        outsample = clamp(outsample,-1.0f,1.0f);
                    else if (olimmode == 3)
                        outsample = std::tanh(outsample);
                    outputs[OUT_AUDIO].setVoltage(outsample*5.0,i);
                }
                outputs[OUT_LOOP_SWITCH].setVoltage(outFrame.samples[4]);
                
            } else if (ochans == 1)
            {
                auto outFrame = outputBuffer.shift();
                float outsample = outFrame.samples[0]*adjustgain;
                if (olimmode == 2)
                    outsample = clamp(outsample,-1.0f,1.0f);
                else if (olimmode == 3)
                    outsample = std::tanh(outsample);
                outputs[OUT_AUDIO].setVoltage(outsample*5.0,0);
                outputs[OUT_AUDIO].setVoltage(outsample*5.0,1);
                outputs[OUT_LOOP_SWITCH].setVoltage(outFrame.samples[4]);
            }
        }
        
        m_playpos = m_bufferplaypos / args.sampleRate;
        
    }
    float m_out_dur = 10.0f;

    float m_playpos = 0.0f;
    int m_bufferplaypos = 0;
    
    bool m_img_data_dirty = false;
    ImgSynth m_syn;
    GrainMixer m_grainsmixer{&m_syn};
    WDL_Resampler m_src;
    rack::dsp::SchmittTrigger rewindTrigger;
    rack::dsp::PulseGenerator loopStartPulse;
    void getImageData(stbi_uc*& ptr,int& w,int& h)
    {
        std::lock_guard<std::mutex> locker(m_mtx);
        ptr = m_img_data;
        w = m_img_w;
        h = m_img_h;   
    }
private:
    stbi_uc* m_img_data = nullptr;
    int m_img_w = 0;
    int m_img_h = 0;
    std::mutex m_mtx;
};
/*
class MySmallKnob : public RoundSmallBlackKnob
{
public:
    XImageSynth* m_syn = nullptr;
    MySmallKnob() : RoundSmallBlackKnob()
    {
        
    }
    void onDragEnd(const event::DragEnd& e) override
    {
        RoundSmallBlackKnob::onDragEnd(e);
        if (m_syn)
        {
            m_syn->reloadImage();
            mLastValue = this->paramQuantity->getValue();
        }
    }
    float mLastValue = 0.0f;
};
*/
class OscDesignerWidget : public TransparentWidget
{
public: 
    XImageSynth* m_syn = nullptr;
    OscDesignerWidget(XImageSynth* s) : m_syn(s)
    {

    }
    void onButton(const event::Button& e) override
    {
        if (e.action == GLFW_RELEASE)
            return;
        float w = box.size.x/(m_syn->m_oscBuilder.getNumHarmonics()-1);
        int index = std::floor(e.pos.x/w);
        float v = rescale(e.pos.y,0.0,300.0,0,-61.0);
        v = clamp(v,-61.0,0.0);
        if (v<-60.0)
            v = -120.0;
        v = rack::dsp::dbToAmplitude(v);
        m_syn->m_oscBuilder.setHarmonic(index,v);
        m_syn->m_oscBuilder.updateOscillator();
        e.consume(nullptr);
    }
    void draw(const DrawArgs &args) override
    {
        if (!m_syn)
            return;
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGB(0,0,0));
        nvgRect(args.vg,0,0,box.size.x,box.size.y);
        nvgFill(args.vg);
        int numharms = m_syn->m_oscBuilder.getNumHarmonics();
        float w = box.size.x / numharms - 2.0f;
        if (w<2.0f)
            w = 2.0f;
        nvgFillColor(args.vg, nvgRGB(0,255,0));
        for (int i=0;i<numharms;++i)
        {
            float v = m_syn->m_oscBuilder.getHarmonic(i);
            if (v>0.0f)
            {
                float xcor = rescale(i,0,numharms-1,0,box.size.x);
                float db = rack::dsp::amplitudeToDb(v);
                if (db>=-60.0)
                {
                    float ycor = rescale(db,-61.0,0.0,0.0,box.size.y);
                    nvgBeginPath(args.vg);
                    nvgRect(args.vg,xcor,box.size.y-ycor,w,ycor);
                    nvgFill(args.vg);
                }
                
            }
            
        }
        nvgRestore(args.vg);
    }
};

struct ChooseScaleItem  : MenuItem
{
    XImageSynth* syn = nullptr;
    int m_index = 0;
    void onAction(const event::Action &e) override
    {
        syn->params[XImageSynth::PAR_FREQMAPPING].setValue(m_index);
    }
};

class MyMenuButton : public LEDBezel
{
public:
    XImageSynth* m_syn = nullptr;
    int whichParam = 0;
    MyMenuButton() : LEDBezel()
    {
        
    }
           
    
    void onButton(const event::Button& e) override
    {
        if (m_syn==nullptr)
            return;
        
        ui::Menu *menu = createMenu();
        auto namelist = m_syn->m_scala_scales;
        namelist.push_front("Harmonic series");
        namelist.push_front("Linear");
        namelist.push_front("Equal tempered per pixel row");
        int i=0;
        for (auto& name : namelist)
        {
             std::string check;
             if (i == (int)m_syn->params[XImageSynth::PAR_FREQMAPPING].getValue())
             {
                check = CHECKMARK_STRING;
             }
             ChooseScaleItem* item = createMenuItem<ChooseScaleItem>(rack::string::filename(name),check);
             item->syn = m_syn;
             item->m_index = i;
             menu->addChild(item);
             ++i;
        }
        e.consume(nullptr);
    }
private:

};

class XImageSynthWidget : public ModuleWidget
{
public:
    OscDesignerWidget* m_osc_design_widget = nullptr;
    XImageSynth* m_synth = nullptr;
    XImageSynthWidget(XImageSynth* m)
    {
        setModule(m);
        m_synth = m;
        box.size.x = 620.0f;
        if (!g_font)
        	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
        
        if (m)
        {
            m_osc_design_widget = new OscDesignerWidget(m);
            m_osc_design_widget->box.pos.x = 0.0;
            m_osc_design_widget->box.pos.y = 0.0;
            m_osc_design_widget->box.size.x = box.size.x;
            m_osc_design_widget->box.size.y = 300.0f;
            addChild(m_osc_design_widget);
        }
        RoundSmallBlackKnob* knob = nullptr;
        addOutput(createOutputCentered<PJ301MPort>(Vec(30, 330), m, XImageSynth::OUT_AUDIO));
        addInput(createInputCentered<PJ301MPort>(Vec(120, 360), m, XImageSynth::IN_PITCH_CV));
        addInput(createInputCentered<PJ301MPort>(Vec(30, 360), m, XImageSynth::IN_RESET));
        addParam(createParamCentered<LEDBezel>(Vec(60.00, 330), m, XImageSynth::PAR_RELOAD_IMAGE));
        addParam(createParamCentered<CKSS>(Vec(60.00, 360), m, XImageSynth::PAR_PLAYBACKMODE));
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(90.00, 315), m, XImageSynth::PAR_DURATION));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(90.00, 340), m, XImageSynth::PAR_GRAIN_PLAYSPEED));
        addInput(createInputCentered<PJ301MPort>(Vec(90.0, 365), m, XImageSynth::IN_GRAINPLAYRATE_CV));

        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(120.00, 330), m, XImageSynth::PAR_PITCH));
        
        // addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(150.00, 330), m, XImageSynth::PAR_FREQMAPPING)); 
        
        MyMenuButton* mbut = nullptr;
        addChild(mbut = createWidgetCentered<MyMenuButton>(Vec(150.00, 330)));
        mbut->m_syn = m;
        
        addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(150.00, 360), m, XImageSynth::PAR_WAVEFORMTYPE));
        knob->snap = true;
        
        addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(180.00, 330), m, XImageSynth::PAR_PRESET_IMAGE));
        knob->snap = true;
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(210.00, 330), m, XImageSynth::PAR_LOOP_START));
        addOutput(createOutputCentered<PJ301MPort>(Vec(240, 330), m, XImageSynth::OUT_LOOP_SWITCH));
        addOutput(createOutputCentered<PJ301MPort>(Vec(240, 360), m, XImageSynth::OUT_LOOP_PHASE));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(210.00, 360), m, XImageSynth::PAR_LOOP_LEN));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(270.00, 330), m, XImageSynth::PAR_FREQUENCY_BALANCE));
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(270.00, 360), m, XImageSynth::PAR_HARMONICS_FUNDAMENTAL));
        
        //addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(300.00, 330), m, XImageSynth::PAR_PAN_MODE));
        //knob->snap = true;
        addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(300.00, 360), m, XImageSynth::PAR_NUMOUTCHANS));
        knob->snap = true;
        addInput(createInputCentered<PJ301MPort>(Vec(330, 330), m, XImageSynth::IN_LOOPSTART_CV));
        addInput(createInputCentered<PJ301MPort>(Vec(330, 360), m, XImageSynth::IN_LOOPLEN_CV));
        addParam(createParamCentered<CKSS>(Vec(360.00, 330), m, XImageSynth::PAR_DESIGNER_ACTIVE));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(360.00, 360), m, XImageSynth::PAR_DESIGNER_VOLUME));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(390.00, 330), m, XImageSynth::PAR_ENVELOPE_SHAPE));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(390.00, 360), m, XImageSynth::PAR_SCALA_TUNING_AMOUNT));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(420.00, 330), m, XImageSynth::PAR_MINPITCH));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(420.00, 360), m, XImageSynth::PAR_MAXPITCH));
        addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(450.00, 330), m, XImageSynth::PAR_LOOPMODE));
        knob->snap = true;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(450.00, 360), m, XImageSynth::PAR_GRAIN_SIZE));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(480.00, 330), m, XImageSynth::PAR_GRAIN_RANDOM));
        addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(480.00, 360), m, XImageSynth::PAR_OUTLIMITMODE));
        knob->snap = true;
    }
    
    ~XImageSynthWidget()
    {
        if (m_ctx && m_image!=0)
            nvgDeleteImage(m_ctx,m_image);
    }
    int imageCreateCounter = 0;
    bool imgDirty = false;
    float hoverYCor = 0.0f;
    void onHover(const event::Hover& e) override
    {
        ModuleWidget::onHover(e);
        hoverYCor = e.pos.y;
    }
    void step() override
    {
        if (m_synth==nullptr)
            return;
        
        if (m_synth->params[XImageSynth::PAR_DESIGNER_ACTIVE].getValue()>0.5)
            m_osc_design_widget->show();
        else
        {
            if (m_osc_design_widget->visible)
            {
                m_osc_design_widget->hide();
                //m_synth->reloadImage();
            }
            
        }
        float p = m_synth->params[0].getValue();
        if (m_synth->reloadTrigger.process(p>0.0f))
        {
            //m_synth->reloadImage();
            
        }
        m_synth->onTimer();
        ModuleWidget::step();
    }
    void draw(const DrawArgs &args) override
    {
        m_ctx = args.vg;
        if (m_synth==nullptr)
            return;
        nvgSave(args.vg);
        int imgw = 0; 
        int imgh = 0; 
        int neww = 0; 
        int newh = 0; 
        stbi_uc* idataptr = nullptr;
        m_synth->getImageData(idataptr,neww,newh);
        if (m_image == 0 && neww>0 && idataptr!=nullptr)
        {
            m_image = nvgCreateImageRGBA(
                args.vg,neww,newh,
                NVG_IMAGE_GENERATE_MIPMAPS,idataptr);
            
            imageCreateCounter+=1;
        }
        nvgImageSize(args.vg,m_image,&imgw,&imgh);
        if (neww!=imgw)
        {
            if (neww>0 && idataptr!=nullptr)
            {
                if (m_image!=0)
                    nvgDeleteImage(args.vg,m_image);
                m_image = nvgCreateImageRGBA(args.vg,neww,newh,NVG_IMAGE_GENERATE_MIPMAPS,idataptr);
                imageCreateCounter+=1;
            }
            
        }
        if (m_synth->m_img_data_dirty && idataptr!=nullptr)
        {
            nvgUpdateImage(args.vg,m_image,idataptr);
            m_synth->m_img_data_dirty = false;
        }
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
        nvgRect(args.vg,0.0f,0.0f,box.size.x,box.size.y);
        nvgFill(args.vg);
        
        
        if (imgw>0 && imgh>0)
        {
            //auto pnt = nvgImagePattern(args.vg,0,0,600.0f,300.0f,0.0f,m_image,1.0f);
            auto pnt = nvgImagePattern(args.vg,0,0,600.0,300.0,0.0f,m_image,1.0f);
            nvgBeginPath(args.vg);
            nvgRect(args.vg,0,0,600,300);
            nvgFillPaint(args.vg,pnt);
            
            nvgFill(args.vg);
        }
        int numfreqs = imgh;
        float minf = m_synth->m_syn.minFrequency;
        float maxf = m_synth->m_syn.maxFrequency;
        nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        nvgStrokeWidth(args.vg,1.0f);
        nvgBeginPath(args.vg);
        for (int i=0;i<numfreqs;++i)
        {
            float ycor = rescale(m_synth->m_syn.currentFrequencies[i],minf,maxf,1.0,299.0);
            
            nvgMoveTo(args.vg,600,ycor);
            nvgLineTo(args.vg,620,ycor);
            
        }
        nvgStroke(args.vg);
            nvgStrokeWidth(args.vg,1.0f);

            nvgBeginPath(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            
            float xcor = rescale(m_synth->m_playpos,0.0,m_synth->m_out_dur,0,600);
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,300);
            nvgStroke(args.vg);

            float loopstart = m_synth->loopstart;
            xcor = rescale(loopstart,0.0,1.0,0,600);
            nvgBeginPath(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0x00, 0xff));
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,300);
            nvgStroke(args.vg);

            float loopend = m_synth->looplen+loopstart;
            if (loopend>1.0f)
                loopend = 1.0f;

            xcor = rescale(loopend,0.0,1.0,0,600);

            nvgBeginPath(args.vg);
            
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,300);
            nvgStroke(args.vg);

            // background for text
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
            nvgRect(args.vg,0,0,600,20);
            nvgFill(args.vg);

            nvgFontSize(args.vg, 15);
            nvgFontFaceId(args.vg, g_font->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            char buf[1000];
            float dirtyElapsed = m_synth->m_syn.getDirtyElapsedTime();
            auto scalefile = rack::string::filename(m_synth->m_syn.currentScalaFile);
            if ((int)m_synth->params[XImageSynth::PAR_FREQMAPPING].getValue()<3)
                scalefile = "";
            int freqIndex = rescale(hoverYCor,0.0f,300.0f,0.0f,599.0f);
            freqIndex = clamp(freqIndex,0,599);
            float hoverFreq = m_synth->m_syn.currentFrequencies[freqIndex];
            float elapsed = m_synth->m_syn.m_elapsedTime;
            float rtfactor = 0.0f;
            if (elapsed>0.0f)
                rtfactor = m_synth->params[XImageSynth::PAR_DURATION].getValue()/elapsed;

            sprintf(buf,"%dx%d (%d %d ic) %d %.1f %s [%.1fHz - %.1fHz %.1fHz] (%.1fx realtime)",imgw,imgh,m_image,imageCreateCounter,m_synth->renderCount,
                dirtyElapsed,scalefile.c_str(),m_synth->m_syn.minFrequency,m_synth->m_syn.maxFrequency,
                hoverFreq,rtfactor);
            nvgText(args.vg, 3 , 10, buf, NULL);
            //sprintf(buf,"%d %d",m_synth->m_grain1.getOutputPos(),
            //    m_synth->m_grain2.getOutputPos());
            //nvgText(args.vg, 3 , 30, buf, NULL);
        float progr = m_synth->m_syn.percentReady();
        if (progr<1.0)
        {
            float progw = rescale(progr,0.0,1.0,0.0,600.0);
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0x00, 0x9f, 0x00, 0xa0));
            nvgRect(args.vg,0.0f,280.0f,progw,20);
            nvgFill(args.vg);
        }
        float dirtyTimer = m_synth->m_syn.getDirtyElapsedTime();
        if (dirtyTimer<=0.5)
        {
            float progw = rescale(dirtyTimer,0.0,0.5,0.0,600.0f);
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x00, 0x00, 0xa0));
            nvgRect(args.vg,0.0f,280.0f,progw,20);
            nvgFill(args.vg);
        }
        
        // 460,330
        nvgBeginPath(args.vg);
        nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        float morph = m_synth->params[XImageSynth::PAR_FREQUENCY_BALANCE].getValue();
        for (int i=0;i<100;i+=2)
        {
            float normx = rescale(i,0,100,0.0,1.0);
            float normy = get_gain_curve_value(morph,normx);
            if (i == 0)
                nvgMoveTo(args.vg,500+i,370-normy*50.0);
            else
                nvgLineTo(args.vg,500+i,370-normy*50.0);
        }
        nvgStroke(args.vg);
        //nvgDeleteImage(args.vg,m_image);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
private:
    NVGcontext* m_ctx = nullptr;
    int m_image = 0;
};

Model* modelXImageSynth = createModel<XImageSynth, XImageSynthWidget>("XImageSynth");
