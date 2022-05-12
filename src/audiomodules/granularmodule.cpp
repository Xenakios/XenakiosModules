//#include "plugin.hpp"
#include "grain_engine.h"
#ifndef RAPIHEADLESS
#include "helperwidgets.h"
#include <osdialog.h>
#else
#include <system.hpp>
#include <jansson.h>
#include <iostream>
#include <string.hpp>
#endif
#include <thread>
#include <mutex>
#include "dr_wav.h"
#include "choc_SingleReaderSingleWriterFIFO.h"

class DrWavSource : public GrainAudioSource
{
public:
    unsigned int m_channels = 0;
    unsigned int m_sampleRate = 44100;
    drwav_uint64 m_totalPCMFrameCount = 0;
    std::vector<float> m_audioBuffer;
    int m_recordChannels = 0;
    float m_recordSampleRate = 0.0f;
    int m_recordState = 0;
    int m_recordBufPos = 0;
    spinlock m_mut;
    //std::mutex m_mut;
    std::atomic<int> m_do_update_peaks{0};
    std::string m_filename;
    void normalize(float level, int startframe, int endframe)
    {
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
        
    }
    void reverse()
    {
        /*
        for (int i=0;i<m_totalPCMFrameCount/2;i++)
        {
            int index=(m_totalPCMFrameCount-i-1);
            if (index<0 || index>=m_totalPCMFrameCount) break;
            for (int j=0;j<m_channels;j++)
            {
                std::swap(m_pSampleData[i*m_channels+j],m_pSampleData[index*m_channels+j]);
            }
        }
        updatePeaks();
        */
    }
    std::mutex m_peaks_mut;
    int m_peak_updates_counter = 0;
    int m_minFramePos = 0;
    int m_maxFramePos = 0;
    void setSubSection(int startFrame, int endFrame) override
    {
        m_minFramePos = clamp(startFrame,0,m_totalPCMFrameCount);
        m_maxFramePos = clamp(endFrame,1,m_totalPCMFrameCount);
    }
    void updatePeaks()
    {
        if (m_do_update_peaks == 0)
            return;
        m_peak_updates_counter++;
        std::lock_guard<std::mutex> locker(m_peaks_mut);
        float* dataPtr = m_audioBuffer.data();
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
    bool saveFile(std::string filename)
    {
        drwav wav;
        drwav_data_format format;
		format.container = drwav_container_riff;
		format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
		format.channels = 2;
		format.sampleRate = m_sampleRate;
		format.bitsPerSample = 32;
        if (drwav_init_file_write(&wav,filename.c_str(),&format,nullptr))
        {
            drwav_write_pcm_frames(&wav,m_audioBuffer.size()/2,(void*)m_audioBuffer.data());
            drwav_uninit(&wav);
            return true;
        }
        return false;
    }
    bool importFile(std::string filename)
    {
        drwav_uint64 totalPCMFrameCount = 0;
        drwav wav;
        if (!drwav_init_file(&wav, filename.c_str(), nullptr))
            return false;
        int framestoread = std::min(m_audioBuffer.size()/2,(size_t)wav.totalPCMFrameCount);
        int inchs = wav.channels;
        std::vector<float> temp(inchs*framestoread);
        drwav_read_pcm_frames_f32(&wav, framestoread, temp.data());
		drwav_uninit(&wav);
        
        for (int i=0;i<framestoread;++i)
        {
            if (inchs == 1)
            {
                for (int j=0;j<2;++j)
                {
                    m_audioBuffer[i*2+j] = temp[i];
                }
            } else if (inchs == 2)
            {
                for (int j=0;j<2;++j)
                {
                    m_audioBuffer[i*2+j] = temp[i*2+j];
                }
            }
            
        }
        
        m_mut.lock();
            m_channels = 2;
            m_sampleRate = wav.sampleRate;
            m_totalPCMFrameCount = framestoread;
            m_recordState = 0;
        
        m_mut.unlock();
        
        m_do_update_peaks = 1;
        m_filename = filename;
        return true;
    }
    std::atomic<int> busy_state{0};
    struct SamplePeaks
    {
        float minpeak = 0.0f;
        float maxpeak = 0.0f;
    };
    std::vector<std::vector<SamplePeaks>> peaksData;
    DrWavSource()
    {
        m_audioBuffer.resize(44100*300*2);
        peaksData.resize(2);
        for (auto& e : peaksData)
            e.resize(1024*1024);

    }
    void clearAudio(int startSample, int endSample)
    {
        
        if (startSample == -1 && endSample == -1)
        {
            startSample = 0;
            endSample = (m_audioBuffer.size() / m_channels)-1;
        }
        startSample = startSample*m_channels;
        endSample = endSample*m_channels;
        if (startSample>=0 && endSample<m_audioBuffer.size())
        {
            for (int i=startSample;i<endSample;++i)
            {
                m_audioBuffer[i] = 0.0f;
            }
            m_do_update_peaks = 1;
        }
    }
    void resetRecording()
    {
        if (m_recordBufPos>=m_audioBuffer.size())
        {
            m_recordBufPos = 0;
        }
    }
    bool m_has_recorded = false;
    int m_recordStartPos = 0;
    std::pair<float,float> getLastRecordedRange()
    {
        float s0 = rescale((float)m_recordStartPos,0.0f,(float)m_audioBuffer.size()-1,0.0f,1.0f);
        float s1 = rescale((float)m_recordBufPos,0.0f,(float)m_audioBuffer.size()-1,0.0f,1.0f);
        return {s0,s1};
    }
    void startRecording(int numchans, float sr)
    {
        if (m_recordState!=0)
            return;
        m_has_recorded = true;
        m_recordChannels = numchans;
        m_recordSampleRate = sr;
        m_recordState = 1;
        m_recordStartPos = m_recordBufPos;
    }
    void pushSamplesToRecordBuffer(float* samples, float gain)
    {
        if (m_recordState == 0)
            return;
        for (int i=0;i<m_recordChannels;++i)
        {
            if (m_recordBufPos<m_audioBuffer.size())
            {
                m_audioBuffer[m_recordBufPos] = samples[i]*gain;
            }
            ++m_recordBufPos;
            if (m_recordBufPos==m_audioBuffer.size())
            {
                std::cout << "RECORD BUFFER FULL, STOPPING\n";
                stopRecording();
                break;
            }
        }
    }
    float getRecordPosition()
    {
        if (m_recordState == 0)
            return -1.0f;
        return 1.0/m_audioBuffer.size()*m_recordBufPos;
    }
    void stopRecording()
    {
        m_recordState = 0;
        m_channels = m_recordChannels;
        m_sampleRate = m_recordSampleRate;
        m_totalPCMFrameCount = m_audioBuffer.size()/m_recordChannels;
        m_do_update_peaks = 1;
    }
    void getSamplesSafeAndFade(float* destbuf,int startframe,int nsamples, int channel, int fadelen) override
    {
        for (int i=0;i<nsamples;++i)
        {
            destbuf[i] = getBufferSampleSafeAndFadeImpl(startframe+i,channel,fadelen);
        }
    }
    float getBufferSampleSafeAndFade(int frame, int channel, int fadelen) override final
    {
        return getBufferSampleSafeAndFadeImpl(frame,channel,fadelen);
    }
    // OK, probably not the most efficient implementation, but will have to see later if can be optimized
    float getBufferSampleSafeAndFadeImpl(int frame, int channel, int fadelen) 
    {
        if (frame>=0 && frame < m_totalPCMFrameCount)
        {
            float gain = 1.0f;
            if (frame>=m_minFramePos && frame<m_minFramePos+fadelen)
                gain = rescale((float)frame,m_minFramePos,m_minFramePos+fadelen,0.0f,1.0f);
            if (frame>=m_maxFramePos-fadelen && frame<m_maxFramePos)
                gain = rescale((float)frame,m_maxFramePos-fadelen,m_maxFramePos,1.0f,0.0f);
            if (frame<m_minFramePos || frame>=m_maxFramePos)
                gain = 0.0f;
            return m_audioBuffer[frame*m_channels+channel] * gain;
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
        float* srcDataPtr = m_audioBuffer.data();
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
                dest[i*channels+j] = getBufferSampleSafeAndFade(index,actsrcchan,fadelen);
            }
        }
    }
    ~DrWavSource()
    {
        if (m_has_recorded)
        {
            #ifndef RAPIHEADLESS
            std::string audioDir = rack::asset::user("XenakiosGrainAudioFiles");
            uint64_t t = system::getUnixTime();
            std::string audioFile = audioDir+"/GrainRec_"+std::to_string(t)+".wav";
            saveFile(audioFile);
            #endif
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

class BufferScrubber
{
public:
    Sinc<float,16,65536> m_sinc_interpolator;
    
    BufferScrubber(DrWavSource* src) : m_src{src}
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
        m_src->setSubSection(srcstartsamples,srcendsamples);
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
                    float y0 = m_src->getBufferSampleSafeAndFade(index0,i,256);
                    float y1 = m_src->getBufferSampleSafeAndFade(index1,i,256);
                    float y2 = y0+(y1-y0)*frac;
                    outbuf[i] = y2 * gain;
                }    
                else
                {
                    float y2 = m_sinc_interpolator.call(*m_src,index0,frac,bogus,i);
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
    DrWavSource* m_src = nullptr;
    
    double m_next_pos = 0.0f;
    std::array<dsp::BiquadFilter,2> m_gain_smoothers;
    std::array<dsp::BiquadFilter,2> m_position_smoothers;
     
};

class GrainEngine
{
public:
    std::unique_ptr<BufferScrubber> m_scrubber;
    GrainEngine()
    {
        DrWavSource* wavsrc = new DrWavSource;
        m_srcs.emplace_back(wavsrc);
        m_gm.reset(new GrainMixer(m_srcs));
        m_markers = {0.0f,1.0f};
        m_scrubber.reset(new BufferScrubber(wavsrc));
    }
    bool isRecording()
    {
        auto src = dynamic_cast<DrWavSource*>(m_srcs[0].get());
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
        #ifdef RAPIHEADLESS
        for (auto& e : m_markers)
            std::cout << e << " ";
        std::cout << "\n";
        #endif
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
            float scrubsmoothcutoff = rescale(std::pow(lenm,2.5f),0.0f,1.0f,0.1f,64.0f);
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
        auto src = dynamic_cast<DrWavSource*>(m_srcs[0].get());
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

#ifndef RAPIHEADLESS

class XGranularModule : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_PLAYRATE,
        PAR_PITCH,
        PAR_LOOPSELECT,
        PAR_LOOPLEN,
        PAR_ATTN_PLAYRATE,
        PAR_ATTN_PITCH,
        PAR_SRCPOSRANDOM,
        PAR_ATTN_LOOPSTART,
        PAR_ATTN_LOOPLEN,
        PAR_GRAINDENSITY,
        PAR_RECORD_ACTIVE,
        PAR_LEN_MULTIP,
        PAR_REVERSE,
        PAR_SOURCESELECT,
        PAR_INPUT_MIX,
        PAR_INSERT_MARKER,
        PAR_LOOP_SLIDE,
        PAR_RESET,
        PAR_ATTN_SRCRND,
        PAR_ATTN_GRAINLEN,
        PAR_PLAYBACKMODE,
        PAR_ATTN_GRAINRATE,
        PAR_LAST
    };
    enum OUTPUTS
    {
        OUT_AUDIO,
        OUT_LOOP_EOC,
        OUT_GRAIN_TRIGGER,
        OUT_LAST
    };
    enum INPUTS
    {
        IN_CV_PLAYRATE,
        IN_CV_PITCH,
        IN_CV_LOOPSTART,
        IN_CV_LOOPLEN,
        IN_AUDIO,
        IN_CV_GRAINRATE,
        IN_RESET,
        IN_CV_SRCRND,
        IN_CV_GRAINLEN,
        IN_LAST
    };
    dsp::BooleanTrigger m_recordTrigger;
    //bool m_recordActive = false;
    dsp::BooleanTrigger m_insertMarkerTrigger;
    dsp::BooleanTrigger m_resetTrigger;
    dsp::SchmittTrigger m_resetInTrigger;
    float m_loopSelectRefState = 0.0f;
    float m_curLoopSelect = 0.0f;
    dsp::ClockDivider exFIFODiv;
    
    XGranularModule()
    {
        std::string audioDir = rack::asset::user("XenakiosGrainAudioFiles");
        rack::system::createDirectory(audioDir);
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_PLAYRATE,-1.0f,1.0f,0.5f,"Playrate");
        configParam(PAR_PITCH,-24.0f,24.0f,0.0f,"Pitch");
        configParam(PAR_LOOPSELECT,-INFINITY,+INFINITY,0.0f,"Region select");
        configParam(PAR_LOOPLEN,0.0f,1.0f,1.0f,"Loop length");
        getParamQuantity(PAR_LOOPLEN)->randomizeEnabled = false;
        configParam(PAR_ATTN_PLAYRATE,-1.0f,1.0f,0.0f,"Playrate CV ATTN");
        configParam(PAR_ATTN_PITCH,-1.0f,1.0f,0.0f,"Pitch CV ATTN");
        getParamQuantity(PAR_ATTN_PITCH)->randomizeEnabled = false;
        configParam(PAR_SRCPOSRANDOM,0.0f,1.0f,0.0f,"Source position randomization");
        configParam(PAR_ATTN_LOOPSTART,-1.0f,1.0f,0.0f,"Loop start CV ATTN");
        getParamQuantity(PAR_ATTN_LOOPSTART)->randomizeEnabled = false;
        configParam(PAR_ATTN_LOOPLEN,-1.0f,1.0f,0.0f,"Loop len CV ATTN");
        configParam(PAR_ATTN_SRCRND,-1.0f,1.0f,0.0f,"Position randomization CV ATTN");
        configParam(PAR_ATTN_GRAINLEN,-1.0f,1.0f,0.0f,"Grain length CV ATTN");
        
        configParam(PAR_GRAINDENSITY,-1.0f,8.0f,3.0f,"Grain rate"," Hz",2,1);
        configParam(PAR_ATTN_GRAINRATE,-1.0f,1.0f,0.0f,"Grain rate CV ATTN");

        configParam(PAR_RECORD_ACTIVE,0.0f,1.0f,0.0f,"Record active");
        getParamQuantity(PAR_RECORD_ACTIVE)->randomizeEnabled = false;
        configParam(PAR_LEN_MULTIP,0.0f,1.0f,0.5f,"Grain length");
        configParam(PAR_REVERSE,0.0f,1.0f,0.0f,"Grain reverse probability");
        configParam(PAR_SOURCESELECT,0.0f,7.0f,0.0f,"Source select");
        configParam(PAR_INPUT_MIX,0.0f,1.0f,0.0f,"Input mix");
        getParamQuantity(PAR_INPUT_MIX)->randomizeEnabled = false;
        configParam(PAR_INSERT_MARKER,0.0f,1.0f,0.0f,"Insert marker");
        getParamQuantity(PAR_INSERT_MARKER)->randomizeEnabled = false;
        configParam(PAR_LOOP_SLIDE,0.0f,1.0f,0.0f,"Loop slide");
        configParam(PAR_RESET,0.0f,1.0f,0.0f,"Reset");
        getParamQuantity(PAR_RESET)->randomizeEnabled = false;
        configSwitch(PAR_PLAYBACKMODE,0,2,0,"Playback mode",
            {"Playrate controls rate",
            "Playrate controls region scan position",
            "Scrub"});
        getParamQuantity(PAR_PLAYBACKMODE)->randomizeEnabled = false;
        exFIFO.reset(64);
        exFIFODiv.setDivision(32768);
    }
    json_t* dataToJson() override
    {
        json_t* resultJ = json_object();
        json_object_set(resultJ,"importedfile",json_string(m_currentFile.c_str()));
        auto markersJ = m_eng.dataToJson();
        json_object_set(resultJ,"markers",markersJ);
        json_object_set(resultJ,"curregionstart",json_real(m_curLoopSelect));
        json_object_set(resultJ,"interpolation_mode",json_integer(m_interpolation_mode));
        return resultJ;
    }
    void dataFromJson(json_t* root) override
    {
        json_t* filenameJ = json_object_get(root,"importedfile");
        if (filenameJ)
        {
            std::string filename(json_string_value(filenameJ));
            importFile(filename);
        }
        json_t* markersJ = json_object_get(root,"markers");
        m_eng.dataFromJson(markersJ);
        json_t* regionPosJ = json_object_get(root,"curregionstart");
        if (regionPosJ) m_curLoopSelect = json_real_value(regionPosJ);
        json_t* iModeJ = json_object_get(root,"interpolation_mode");
        if (iModeJ) m_interpolation_mode = json_integer_value(iModeJ);
    }
    void importFile(std::string filename)
    {
        if (filename.size()==0)
            return;
        auto drsrc = dynamic_cast<DrWavSource*>(m_eng.m_srcs[0].get());
        if (drsrc && drsrc->importFile(filename))
        {
            m_currentFile = filename;
        }
    }
    std::string m_currentFile;
    
    inline float getNotchedPlayRate(float x)
    {
        const std::array<float,3> notchpoints{-0.5f,0.0f,0.5f};
        const float notchrange = 0.05f;
        for (auto& p : notchpoints )
            if(std::abs(x-p) < notchrange) return p;
        return x;
    }
    float m_notched_rate = 0.0f;
    float m_cur_srcposrnd = 0.0f;
    float m_cur_playspeed = 0.0f;
    void process(const ProcessArgs& args) override
    {
        if (exFIFODiv.process())
        {
            std::function<void(void)> func;
            while (exFIFO.pop(func))
            {
                func();
            }
            exFIFODiv.reset();
        }
        float prate = params[PAR_PLAYRATE].getValue();
        float scanpos = rescale(prate,-1.0f,1.0f,0.0f,1.0f);
        float cvpratescan = inputs[IN_CV_PLAYRATE].getVoltage()*params[PAR_ATTN_PLAYRATE].getValue()*0.2f;
        int playmode = params[PAR_PLAYBACKMODE].getValue();
        m_eng.m_playmode = playmode;
        m_eng.m_gm->m_interpmode = m_interpolation_mode;
        if (playmode > 0)
        {
            scanpos += cvpratescan*0.5f;
            if (playmode == 1)
                scanpos = wrap_value_safe(0.0f,scanpos,1.0f);
            else if (playmode == 2)
                scanpos = reflect_value_safe(0.0f,scanpos,1.0f);
            m_eng.m_scanpos = scanpos;
            
        }
        prate = getNotchedPlayRate(prate);
        m_notched_rate = prate;
        prate += cvpratescan;
        prate = clamp(prate,-1.0f,1.0f);
        if (prate<0.0f)
        {
            prate = std::abs(prate);
            if (prate < 0.5f)
                prate = std::pow(prate*2.0f,2.0f);
            else
                prate = 1.0f+std::pow((prate-0.5f)*2.0f,2.0f);
            prate = -prate;
        } else
        {
            if (prate < 0.5f)
                prate = std::pow(prate*2.0f,2.0f);
            else
                prate = 1.0f+std::pow((prate-0.5f)*2.0f,2.0f);
        }
        prate = clamp(prate,-2.0f,2.0f);
        m_cur_playspeed = prate;
        float pitch = params[PAR_PITCH].getValue();
        float tempa = params[PAR_ATTN_PITCH].getValue();
        if (tempa>=0.0f)
        {
            tempa = std::pow(tempa,2.0f);
        } else
        {
            tempa = -std::pow(tempa,2.0f);
        }
        int pitch_channels = inputs[IN_CV_PITCH].getChannels();
        if (pitch_channels == 1)
        {
            pitch += inputs[IN_CV_PITCH].getVoltage()*12.0f*tempa;
            pitch = clamp(pitch,-36.0f,36.0f);
            m_eng.m_gm->m_polypitches_to_use = 0;
        } else if (pitch_channels>1)
        {
            // activate "arpeggiator" mode for grain pitches
            m_eng.m_gm->m_polypitches_to_use = pitch_channels;
            for (int i=0;i<pitch_channels;++i)
            {
                m_eng.m_gm->m_polypitches[i] = inputs[IN_CV_PITCH].getVoltage(i)*12.0f*tempa;
            } 
        }
        if (pitch_channels == 0)
            m_eng.m_gm->m_polypitches_to_use = 0;
        if (playmode == 2)
        {
            m_eng.m_scrubber->m_resampler_type = m_interpolation_mode;
            float sep = params[PAR_PITCH].getValue();
            sep = rescale(sep,-24.0f,24.0f,0.0f,1.0f);
            sep += inputs[IN_CV_PITCH].getVoltage()*params[PAR_ATTN_PITCH].getValue()*0.1;
            sep = clamp(sep,0.0f,1.0f);
            m_eng.m_scrubber->setSeparation(sep);
        }
        // more complicated than usual because of the infinitely turning knob
        float loopstartDelta = 0.25f*(params[PAR_LOOPSELECT].getValue() - m_loopSelectRefState);
        float loopstart = m_curLoopSelect;
        if (loopstartDelta!=0.0f)
        {
            loopstart = m_curLoopSelect + loopstartDelta;
            loopstart = wrap_value_safe(0.0f,loopstart,1.0f);
            m_loopSelectRefState = params[PAR_LOOPSELECT].getValue();
        }
        m_curLoopSelect = loopstart;
        loopstart += inputs[IN_CV_LOOPSTART].getVoltage()*params[PAR_ATTN_LOOPSTART].getValue()*0.1f;
        loopstart = wrap_value_safe(0.0f,loopstart,1.0f);
        
        float looplen = params[PAR_LOOPLEN].getValue();
        looplen += inputs[IN_CV_LOOPLEN].getVoltage()*params[PAR_ATTN_LOOPLEN].getValue()*0.1f;
        looplen = clamp(looplen,0.0f,1.0f);
        
        float posrnd = params[PAR_SRCPOSRANDOM].getValue();
        posrnd += inputs[IN_CV_SRCRND].getVoltage()*params[PAR_ATTN_SRCRND].getValue()*0.1f;
        posrnd = clamp(posrnd,0.0f,1.0f);
        posrnd = std::pow(posrnd,2.0f);
        m_cur_srcposrnd = posrnd;

        float grate = params[PAR_GRAINDENSITY].getValue();
        grate += inputs[IN_CV_GRAINRATE].getVoltage()*params[PAR_ATTN_GRAINRATE].getValue();
        grate = clamp(grate,-2.0f,9.0f);
        grate *= 12.0f;
        grate = std::pow(2.0f,1.0f/12*grate);
        
        float glenm = params[PAR_LEN_MULTIP].getValue();
        glenm += inputs[IN_CV_GRAINLEN].getVoltage() * params[PAR_ATTN_GRAINLEN].getValue()*0.1f;
        glenm = clamp(glenm,0.0f,1.0f);

        float revprob = params[PAR_REVERSE].getValue();
        auto drsrc = dynamic_cast<DrWavSource*>(m_eng.m_srcs[0].get());
        if (m_recordTrigger.process(params[PAR_RECORD_ACTIVE].getValue()>0.5f))
        {
            
            if (m_eng.isRecording() == false)
            {
                drsrc->startRecording(2,args.sampleRate);
                
            }
            else
            {
                m_eng.addMarkerAtPosition(drsrc->getRecordPosition());
                drsrc->stopRecording();
            }
        }
        if (m_resetTrigger.process(params[PAR_RESET].getValue()>0.5f))
        {
            m_eng.m_gm->seekPercentage(0.0f);
        }
        if (m_resetInTrigger.process(inputs[IN_RESET].getVoltage()))
        {
            m_eng.m_gm->seekPercentage(0.0f);
        }
        float recbuf[2] = {0.0f,0.0f};
        int inchans = inputs[IN_AUDIO].getChannels();
        if (inchans==1)
        {
            recbuf[0] = inputs[IN_AUDIO].getVoltage();
            recbuf[1] = recbuf[0];
        } else
        {
            recbuf[0] = inputs[IN_AUDIO].getVoltage(0);
            recbuf[1] = inputs[IN_AUDIO].getVoltage(1);
        }
        
        float buf[4] ={0.0f,0.0f,0.0f,0.0f};
        if (m_eng.isRecording())
            drsrc->pushSamplesToRecordBuffer(recbuf,0.199f);
        int srcindex = params[PAR_SOURCESELECT].getValue();
        float loopslide = params[PAR_LOOP_SLIDE].getValue();
        float pitchspread = 0.0f;
        if (!inputs[IN_CV_PITCH].isConnected())
            pitchspread = params[PAR_ATTN_PITCH].getValue();
        m_eng.process(args.sampleTime, args.sampleRate, buf,prate,pitch,loopstart,looplen,loopslide,
            posrnd,grate,glenm,revprob, srcindex, pitchspread);
        outputs[OUT_AUDIO].setChannels(2);
        float inmix = params[PAR_INPUT_MIX].getValue();
        float invmix = 1.0f - inmix;
        float procout0 = std::tanh(buf[0]);
        float procout1 = std::tanh(buf[1]);
        
        float out0 = (invmix * procout0 * 5.0f) + inmix * recbuf[0];
        float out1 = (invmix * procout1 * 5.0f) + inmix * recbuf[0];
        if (inchans == 2)
            out1 = (invmix * procout1 * 5.0f) + inmix * recbuf[1];
        outputs[OUT_AUDIO].setVoltage(out0 , 0);
        outputs[OUT_AUDIO].setVoltage(out1 , 1);
        outputs[OUT_LOOP_EOC].setVoltage(m_eng.m_gm->m_loop_eoc_out);
        outputs[OUT_GRAIN_TRIGGER].setVoltage(m_eng.m_gm->m_grain_trig_out);
        if (m_insertMarkerTrigger.process(params[PAR_INSERT_MARKER].getValue()>0.5f))
        {
            m_eng.addMarker();
        }

        if (m_next_marker_action == ACT_RESET_RECORD_HEAD)
        {
            drsrc->resetRecording();
            m_next_marker_action = ACT_NONE;
        }
        
        graindebugcounter = m_eng.m_gm->grainCounter;
    }
    void clearRegionAudio()
    {
        auto drsrc = dynamic_cast<DrWavSource*>(m_eng.m_srcs[0].get());
        int startSample = m_eng.m_gm->m_region_start * m_eng.m_gm->m_inputdur;
        int endSample = startSample + (m_eng.m_gm->m_region_len * m_eng.m_gm->m_inputdur);
        drsrc->clearAudio(startSample,endSample);
    }
    int graindebugcounter = 0;
    void normalizeAudio(int opts, float peakgain)
    {
        auto drsrc = dynamic_cast<DrWavSource*>(m_eng.m_srcs[0].get());
        if (opts == 0)
            drsrc->normalize(peakgain,-1,-1);
        if (opts == 1)
        {
            float loopstart = m_eng.m_gm->m_actLoopstart; 
            int startframe = loopstart * drsrc->getSourceNumSamples();
            int endframe = m_eng.m_gm->m_actLoopend * drsrc->getSourceNumSamples();
            drsrc->normalize(peakgain, startframe,endframe);
        }
        if (opts == 2)
        {
            auto& ms = m_eng.m_markers;
            for (int i=0;i<ms.size()-1;++i)
            {
                float loopstart = ms[i];
                float loopend = ms[i+1];
                int startframe = loopstart * drsrc->getSourceNumSamples();
                int endframe = loopend * drsrc->getSourceNumSamples();
                drsrc->normalize(peakgain, startframe,endframe);
            }
        }
    }
    enum ACTIONS
    {
        ACT_NONE,
        ACT_CLEAR_ALL_MARKERS,
        ACT_RESET_RECORD_HEAD,
        ACT_CLEAR_ALL_AUDIO,
        ACT_CLEAR_REGION,
        ACT_ADD_EQ_MARKERS,
        ACT_LAST
    };
    std::atomic<int> m_next_marker_action{ACT_NONE};
    choc::fifo::SingleReaderSingleWriterFIFO<std::function<void(void)>> exFIFO;
    GrainEngine m_eng;
    int m_interpolation_mode = 0;
private:
    
};

struct LoadFileItem : MenuItem
{
    XGranularModule* m_mod = nullptr;
    void onAction(const event::Action &e) override
    {
        std::string dir = asset::plugin(pluginInstance, "/res");
        osdialog_filters* filters = osdialog_filters_parse("WAV file:wav");
        char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
        osdialog_filters_free(filters);
        if (!pathC) {
            return;
        }
        std::string path = pathC;
        std::free(pathC);
        m_mod->importFile(path);
    }
};

class RecButton : public app::SvgSwitch
{
public:
    RecButton()
    {
        this->momentary = true;
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/RedButton.svg")));
    }
};

class WaveFormWidget : public TransparentWidget
{
public:
    XGranularModule* m_gm = nullptr;
    int m_opts = 0;
    WaveFormWidget(XGranularModule* m, int opts) : m_gm(m), m_opts(opts)
    {

    }
    void draw(const DrawArgs &args) override
    {
        if (!m_gm)
            return;
        nvgSave(args.vg);

        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
        nvgRect(args.vg,0.0f,0.0f,box.size.x,box.size.y);
        nvgFill(args.vg);

        auto& src = *dynamic_cast<DrWavSource*>(m_gm->m_eng.m_srcs[0].get());
        if (src.m_channels>0)
        {
            std::lock_guard<std::mutex> locker(src.m_peaks_mut);
            int numpeaks = box.size.x;
            int numchans = src.m_channels;
            float numsrcpeaks = src.peaksData[0].size();
            auto regionrange = m_gm->m_eng.getActiveRegionRange();
            float loopstartnorm = regionrange.first;
            float loopendnorm = regionrange.second;
            float startpeaks = 0.0f ;
            float endpeaks = numsrcpeaks ;
            if (m_opts == 1)
            {
                startpeaks = loopstartnorm * numsrcpeaks ;
                endpeaks = loopendnorm * numsrcpeaks ;
            }
            float chanh = box.size.y/numchans;
            
            //nvgStrokeWidth(args.vg,0.5f);
            for (int i=0;i<numchans;++i)
            {
                
                auto drawf = [&,this](int which)
                {
                    nvgBeginPath(args.vg);
                    for (int j=0;j<numpeaks;++j)
                    {
                        float index = rescale(j,0,numpeaks,startpeaks,endpeaks-1.0f);
                        if (index>=0.0f && index<numsrcpeaks)
                        {
                            int index_i = std::round(index);
                            float minp = src.peaksData[i][index_i].minpeak;
                            float maxp = src.peaksData[i][index_i].maxpeak;
                            
                            float ycor0 = 0.0f;
                            if (which == 0)
                                ycor0 = rescale(minp,-1.0f,1.0,chanh,0.0f);
                            else
                                ycor0 = rescale(maxp,-1.0f,1.0,chanh,0.0f);
                            if (j==0)
                                nvgMoveTo(args.vg,j,chanh*i+ycor0);
                            nvgLineTo(args.vg,j+1,chanh*i+ycor0);
                        }
                    }
                    nvgStroke(args.vg);
                };
                nvgStrokeColor(args.vg,nvgRGBA(0xee, 0xee, 0xee, 0xff));
                drawf(0);
                nvgStrokeColor(args.vg,nvgRGBA(0xee, 0xee, 0x00, 0xff));
                drawf(1);
            }
            
            nvgStrokeWidth(args.vg,1.0f);
            if (m_opts == 1)
            {
                
                int numActiveGrains = m_gm->m_eng.m_gm->m_grainsUsed;
                for (int i=0;i<16;++i)
                {
                    auto info = m_gm->m_eng.m_gm->getGrainSourcePositionAndGain(i);
                    if (info.first>=0.0f)
                    {
                        float srcdur = m_gm->m_eng.m_gm->m_inputdur;
                        float xcor = rescale(info.first,loopstartnorm,loopendnorm,0.0f,box.size.x);
                        float ycor0 = rescale(i,0,10,2.0f,box.size.y-2);
                        nvgBeginPath(args.vg);
                        int alpha = rescale(info.second,0.0f,1.0f,0,255);
                        nvgFillColor(args.vg,nvgRGBA(0xff, 0xff, 0xff, alpha));        
                        nvgCircle(args.vg,xcor,ycor0,5.0f);
                        nvgFill(args.vg);
                    }
                    
                }
                
                

                
                if (m_gm->m_eng.m_playmode == 2)
                {
                    float regionlen = regionrange.second-regionrange.first;
                    nvgBeginPath(args.vg);
                    nvgStrokeColor(args.vg,nvgRGBA(0xff, 0xff, 0xff, 255));
                    nvgStrokeWidth(args.vg,3.0f);
                    float pheadhei = box.size.y / 2;
                    for (int i=0;i<2;++i)
                    {
                        float tpos = rescale(m_gm->m_eng.m_scrubber->m_smoothed_positions[i],regionrange.first,regionrange.second, 0.0f,1.0f);
                        tpos = rescale(tpos,0.0f,1.0f,0.0f,box.size.x);
                        tpos = clamp(tpos,0.0f,box.size.x);
                    
                        nvgMoveTo(args.vg,tpos,i*pheadhei);
                        nvgLineTo(args.vg,tpos,i*pheadhei+pheadhei);
                    
                    }
                    nvgStroke(args.vg);
                }
            }
            if (m_opts == 0)
            {
                nvgBeginPath(args.vg); 
                nvgStrokeColor(args.vg,nvgRGBA(0x00, 0xff, 0xff, 0xff));
                auto& markers = m_gm->m_eng.m_markers; 
                for (int i=0;i<markers.size();++i)
                {
                    float xcor = rescale(markers[i],0.0f,1.0f,0.0f,box.size.x);
                    nvgMoveTo(args.vg,xcor,0.0f);
                    nvgLineTo(args.vg,xcor,box.size.y);
                }
                nvgStroke(args.vg);
                
                nvgBeginPath(args.vg);
                nvgFillColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0x80));
                auto regionrange = m_gm->m_eng.getActiveRegionRange();
                float loopstart = regionrange.first;
                float loopend = regionrange.second;
                float loopw = rescale(loopend-loopstart,0.0f,1.0f,0.0f,box.size.x);
                float xcor = rescale(loopstart,0.0f,1.0f,0.0f,box.size.x);
                nvgRect(args.vg,xcor,0.0f,loopw,box.size.y);
                nvgFill(args.vg);

                auto recrange = src.getLastRecordedRange();
                if (src.m_recordState !=0 && recrange.second>0.0f)
                {
                    nvgBeginPath(args.vg);
                    nvgFillColor(args.vg,nvgRGBA(0xff, 0x00, 0x00, 127));
                    float xcor0 = rescale(recrange.first,0.0f,1.0f,0.0f,box.size.x);
                    float xcor1 = rescale(recrange.second,0.0f,1.0f,0.0f,box.size.x);
                    if (xcor0>=0.0f && xcor1<box.size.x)
                    {
                        nvgRect(args.vg,xcor0,0.0f,xcor1-xcor0,box.size.y);
                        nvgFill(args.vg);
                    }
                    
                }
                if (m_gm->m_eng.m_playmode < 2)
                {
                    float tpos = m_gm->m_eng.m_gm->getSourcePlayPosition();
                    float srclen = m_gm->m_eng.m_gm->m_inputdur;
                    
                    tpos = rescale(tpos,0.0f,srclen,0.0f,box.size.x);
                    tpos = clamp(tpos,0.0f,box.size.x);
                    nvgBeginPath(args.vg);
                    nvgStrokeColor(args.vg,nvgRGBA(0xff, 0xff, 0xff, 255));
                    nvgMoveTo(args.vg,tpos,0.0f);
                    nvgLineTo(args.vg,tpos,box.size.y);
                    nvgStroke(args.vg);
                }
                if (m_gm->m_eng.m_playmode == 2)
                {
                    nvgBeginPath(args.vg);
                    nvgStrokeColor(args.vg,nvgRGBA(0xff, 0xff, 0xff, 255));
                    nvgStrokeWidth(args.vg,3.0f);
                    float phedhei = box.size.y/2;
                    for (int i=0;i<2;++i)
                    {
                        float tpos = m_gm->m_eng.m_scrubber->m_smoothed_positions[i];
                        tpos = rescale(tpos,0.0f,1.0f,0.0f,box.size.x);
                        tpos = clamp(tpos,0.0f,box.size.x);
                        nvgMoveTo(args.vg,tpos,i*phedhei);
                        nvgLineTo(args.vg,tpos,i*phedhei+phedhei);
                    }
                    
                    nvgStroke(args.vg);
                }

            }
        }
        nvgRestore(args.vg);
    }
};

template<typename F,typename FIFO>
inline rack::MenuItem* createSafeMenuItem(F func,std::string text,FIFO& fifo)
{
    auto item = createMenuItem([func,&fifo]() 
    { 
        fifo.push(func);
    },text);
    return item;
}

class XGranularWidget : public rack::ModuleWidget
{
public:
    XGranularModule* m_gm = nullptr;
    
	void appendContextMenu(Menu *menu) override 
    {
		auto loadItem = createMenuItem<LoadFileItem>("Import .wav file...");
		loadItem->m_mod = m_gm;
		menu->addChild(loadItem);
        auto drsrc = dynamic_cast<DrWavSource*>(m_gm->m_eng.m_srcs[0].get());
        auto procaudiomenufunc = [this,drsrc](Menu* targmenu)
        {
            auto normItem = createMenuItem([this](){ m_gm->normalizeAudio(0,1.0f); },"Normalize all audio");
            targmenu->addChild(normItem);
            normItem = createMenuItem([this](){ m_gm->normalizeAudio(1,1.0f); },"Normalize active region audio");
            targmenu->addChild(normItem);
            normItem = createMenuItem([this](){ m_gm->normalizeAudio(2,1.0f); },"Normalize all regions audio separately");
            targmenu->addChild(normItem);
            auto revItem = createMenuItem([this,drsrc](){ drsrc->reverse(); },"Reverse audio");
            targmenu->addChild(revItem);
            auto clearall = createMenuItem([this,drsrc]()
            { 
                m_gm->exFIFO.push([this,drsrc]()
                {
                    drsrc->clearAudio(-1,-1);
                });
                 
            },"Clear all audio");
            targmenu->addChild(clearall);
            auto clearregion = createMenuItem([this]()
            { 
                m_gm->exFIFO.push([this]()
                {
                    m_gm->clearRegionAudio();
                });
            }
            ,"Clear active region audio");
            targmenu->addChild(clearregion);
        };
        auto procaudiomenu = createSubmenuItem("Process audio","",procaudiomenufunc);
        menu->addChild(procaudiomenu);
        
        auto clearmarksItem = createSafeMenuItem([this]()
        {
            m_gm->m_eng.clearMarkers();
        },"Clear all markers",m_gm->exFIFO);
        menu->addChild(clearmarksItem);
        
        
        auto submenufunc = [this](Menu* targmenu)
        {
            std::array<int,6> temp{4,5,8,16,32,100};
            for (size_t i=0;i<temp.size();++i)
            {
                auto it = createMenuItem([this,i,temp]()
                {    
                    m_gm->exFIFO.push([this,i,temp]()
                    {
                        m_gm->m_eng.addEquidistantMarkers(temp[i]);
                    });
                },std::to_string(temp[i])+" markers");
                targmenu->addChild(it);
            }
        };
        auto markermenu = createSubmenuItem("Create markers automatically","",submenufunc);
        menu->addChild(markermenu);
        
        auto resetrec = createMenuItem([this]()
        { m_gm->m_next_marker_action = XGranularModule::ACT_RESET_RECORD_HEAD; },"Reset record state");
        menu->addChild(resetrec);
        
        auto scrubopt = createMenuItem([this]()
        { 
            if (m_gm->m_interpolation_mode == 0)
                m_gm->m_interpolation_mode = 1;
            else m_gm->m_interpolation_mode = 0;
        }
        ,"Sinc interpolation (CPU intensive)",CHECKMARK(m_gm->m_interpolation_mode == 1));
        menu->addChild(scrubopt);
        scrubopt = createMenuItem([this]()
        { 
            if (m_gm->m_eng.m_scrubber->m_compensate_volume == 0)
                m_gm->m_eng.m_scrubber->m_compensate_volume = 1;
            else m_gm->m_eng.m_scrubber->m_compensate_volume = 0;
        }
        ,"Compensate volume for scrub mode",CHECKMARK(m_gm->m_eng.m_scrubber->m_compensate_volume == 1));
        menu->addChild(scrubopt);
    }
    XGranularWidget(XGranularModule* m)
    {
        setModule(m);
        m_gm = m;
        box.size.x = RACK_GRID_WIDTH*21;
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "GRAINS",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        
        auto port = new PortWithBackGround(m,this,XGranularModule::OUT_AUDIO,1,17,"AUDIO OUT 1",true);
        port = new PortWithBackGround(m,this,XGranularModule::OUT_LOOP_EOC,92,17,"LOOP EOC",true);
        port = new PortWithBackGround(m,this,XGranularModule::OUT_GRAIN_TRIGGER,92+28.5,17,"GRAIN TRIG",true);
        port = new PortWithBackGround(m,this,XGranularModule::IN_AUDIO,34,17,"AUDIO IN",false);
        
        addParam(createParam<TL1105>(Vec(62,34),m,XGranularModule::PAR_RECORD_ACTIVE));
        addParam(createParam<TL1105>(Vec(150,34),m,XGranularModule::PAR_INSERT_MARKER));
        addParam(createParam<TL1105>(Vec(180,34),m,XGranularModule::PAR_RESET));
        port = new PortWithBackGround(m,this,XGranularModule::IN_RESET,180,17,"RST",false);
        addParam(createParam<Trimpot>(Vec(62,14),m,XGranularModule::PAR_INPUT_MIX));
        
        addChild(new KnobInAttnWidget(this,
            "PLAYRATE",XGranularModule::PAR_PLAYRATE,
            XGranularModule::IN_CV_PLAYRATE,XGranularModule::PAR_ATTN_PLAYRATE,1.0f,60.0f));
        addParam(createParam<CKSSThreeHorizontal>(Vec(55.0, 60.0), module, XGranularModule::PAR_PLAYBACKMODE));
        
        addChild(new KnobInAttnWidget(this,
            "PITCH",XGranularModule::PAR_PITCH,XGranularModule::IN_CV_PITCH,XGranularModule::PAR_ATTN_PITCH,82.0f,60.0f));
        addChild(new KnobInAttnWidget(this,
            "LOOP SLIDE",XGranularModule::PAR_LOOP_SLIDE,-1,-1,2*82.0f,60.0f));
        addChild(new KnobInAttnWidget(this,"REGION SELECT",
            XGranularModule::PAR_LOOPSELECT,XGranularModule::IN_CV_LOOPSTART,XGranularModule::PAR_ATTN_LOOPSTART,1.0f,101.0f));
        addChild(new KnobInAttnWidget(this,"LOOP LENGTH",
            XGranularModule::PAR_LOOPLEN,XGranularModule::IN_CV_LOOPLEN,XGranularModule::PAR_ATTN_LOOPLEN,82.0f,101.0f));
        addChild(new KnobInAttnWidget(this,"SOURCE POS RAND",
            XGranularModule::PAR_SRCPOSRANDOM,XGranularModule::IN_CV_SRCRND,XGranularModule::PAR_ATTN_SRCRND,1.0f,142.0f));
        addChild(new KnobInAttnWidget(this,"GRAIN RATE",
            XGranularModule::PAR_GRAINDENSITY,XGranularModule::IN_CV_GRAINRATE,XGranularModule::PAR_ATTN_GRAINRATE,
            82.0f,142.0f));
        addChild(new KnobInAttnWidget(this,"GRAIN LEN",XGranularModule::PAR_LEN_MULTIP,
            XGranularModule::IN_CV_GRAINLEN,XGranularModule::PAR_ATTN_GRAINLEN,2*82.0f,142.0f));
        addChild(new KnobInAttnWidget(this,"GRAIN REVERSE",XGranularModule::PAR_REVERSE,-1,-1,2*82.0f,101.0f));
        
        WaveFormWidget* wavew = new WaveFormWidget(m,0);
        wavew->box.pos = {1.0f,215.0f};
        wavew->box.size = {box.size.x-2.0f,48.0f};
        addChild(wavew); 
        wavew = new WaveFormWidget(m,1);
        wavew->box.pos = {1.0f,265.0f};
        wavew->box.size = {box.size.x-2.0f,108.0f};
        addChild(wavew);
    }
    void step() override
    {
        if (m_gm)
        {
            auto& src = *dynamic_cast<DrWavSource*>(m_gm->m_eng.m_srcs[0].get());
            src.updatePeaks();
        }
        
        ModuleWidget::step();
    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x50, 0x50, 0x50, 0xff));
        nvgRect(args.vg,0.0f,0.0f,box.size.x,box.size.y);
        nvgFill(args.vg);
        if (m_gm)
        {
            char buf[200];
            auto& src = *dynamic_cast<DrWavSource*>(m_gm->m_eng.m_srcs[0].get());
            std::string rectext;
            if (m_gm->m_eng.isRecording())
                rectext = "REC";
            if (m_gm->m_eng.m_scrubber->m_stopped)
                rectext =  "STOPPED  ";
            else rectext = "SCRUBBING";
            double scrubrate = m_gm->m_eng.m_scrubber->m_smoothed_out_gain;
            sprintf(buf,"%d %d %f %s %d %f %f",
                m_gm->graindebugcounter,m_gm->m_eng.m_gm->m_grainsUsed,scrubrate,
                rectext.c_str(),src.m_peak_updates_counter,m_gm->m_curLoopSelect,m_gm->m_cur_playspeed);
            nvgFontSize(args.vg, 15);
            nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            
            nvgText(args.vg, 1 , 210, buf, NULL);

        }
        


        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelXGranular = createModel<XGranularModule, XGranularWidget>("XGranular");

#else

#include "portaudio.h"
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include "rtmidi/RtMidi.h"

char mygetch() {
    char buf = 0;
    struct termios old = { 0 };
    fflush(stdout);
    if (tcgetattr(0, &old) < 0) perror("tcsetattr()");
    old.c_lflag    &= ~ICANON;   // local modes = Non Canonical mode
    old.c_lflag    &= ~ECHO;     // local modes = Disable echo. 
    old.c_cc[VMIN]  = 1;         // control chars (MIN value) = 1
    old.c_cc[VTIME] = 0;         // control chars (TIME value) = 0 (No time)
    if (tcsetattr(0, TCSANOW, &old) < 0) perror("tcsetattr ICANON");
    if (read(0, &buf, 1) < 0) perror("read()");
    old.c_lflag    |= ICANON;    // local modes = Canonical mode
    old.c_lflag    |= ECHO;      // local modes = Enable echo. 
    if (tcsetattr(0, TCSADRAIN, &old) < 0) perror ("tcsetattr ~ICANON");
    return buf;
 }


class AudioEngine
{
public:
    GrainEngine* m_eng = nullptr;
    PaStreamParameters outputParameters;
    PaStreamParameters inputParameters;
    PaStream *stream = nullptr;
    bool inited = false;
    AudioEngine(GrainEngine* e) : m_eng(e)
    {
        m_cur_playstate = m_eng->m_playmode;
        std::cout << "attempting to start portaudio\n";
        m_dc_blockers[0].setParameters(rack::dsp::BiquadFilter::HIGHPASS_1POLE,30.0f/44100.0,1.0,1.0f);
        m_dc_blockers[1].setParameters(rack::dsp::BiquadFilter::HIGHPASS_1POLE,30.0f/44100.0,1.0,1.0f);
        m_drywetsmoother.setParameters(rack::dsp::BiquadFilter::LOWPASS_1POLE,10.0f/44100.0,1.0,1.0f);
        PaError err;
        err = Pa_Initialize();
        if (err == paNoError)
            inited = true;
        else return;
        printError(err);
        for (int i=0;i<Pa_GetDeviceCount();++i)
        {
            std::cout << i << " :: " << Pa_GetDeviceInfo(i)->name << "\n";
        }
        outputParameters.device = 0; // Pa_GetDefaultOutputDevice(); /* default output device */
        inputParameters.device = 0; //Pa_GetDefaultInputDevice();
        if (inputParameters.device == paNoDevice)
        {
            std::cout << "no default input device\n";
        }
        if (outputParameters.device == paNoDevice) 
        {
            fprintf(stderr,"Error: No default output device.\n");
        } else
        {
            inputParameters.channelCount = 2;
            inputParameters.sampleFormat = paFloat32;
            inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
            
            outputParameters.channelCount = 2;       /* stereo output */
            outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
            outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
            std::cout << Pa_GetDeviceInfo(inputParameters.device)->name << "\n";
            std::cout << "portaudio default low in latency " << inputParameters.suggestedLatency << "\n";
            std::cout << "portaudio default low out latency " << outputParameters.suggestedLatency << "\n";
            inputParameters.hostApiSpecificStreamInfo = NULL;
            outputParameters.hostApiSpecificStreamInfo = NULL;
            //return 0;
            err = Pa_OpenStream(
                    &stream,
                    &inputParameters, 
                    &outputParameters,
                    44100,
                    512,
                    paClipOff,      /* we won't output out of range samples so don't bother clipping them */
                    paCallback,
                    this );
            printError(err);
            err = Pa_StartStream( stream );
            printError(err);
        }

    
    }
    void printError(PaError e)
    {
        if (e == paNoError)
            return;
        fprintf( stderr, "An error occured while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", e );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( e ) );
    }
    ~AudioEngine()
    {
        std::cout << "finishing portaudio\n";
        if (stream)
        {
            Pa_StopStream(stream);
            Pa_CloseStream( stream );
        }
        if (inited)
            Pa_Terminate();
    }
    static int paCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
    {
        AudioEngine* eng = (AudioEngine*)userData;
        memset(outputBuffer,0,sizeof(float)*2*framesPerBuffer); // not known yet if the output buffer is precleared
        if (eng->m_cbcount<50) // little hack to do no work at startup
        {
            ++eng->m_cbcount;
            
            return paContinue;
        }
        float sr = 44100.0f;
        auto drsrc = dynamic_cast<DrWavSource*>(eng->m_eng->m_srcs[0].get());
        bool rec_active = eng->m_eng->isRecording();
        if (eng->m_toggle_record == true)
        {
            if (rec_active == false)
            {
                drsrc->startRecording(2,sr);
                rec_active = true;
                std::cout << "START RECORDING\n";
            } else
            {
                eng->m_eng->addMarkerAtPosition(drsrc->getRecordPosition());
                drsrc->stopRecording();
                rec_active = false;
                std::cout << "END RECORDING\n";
            }
            eng->m_toggle_record = false;
        }
        if (eng->m_add_marker == true)
        {
            eng->m_add_marker = false;
            eng->m_eng->addMarker();
        }
        float deltatime = 1.0f/sr;
        float procbuf[4] = {0.0f,0.0f,0.0f,0.0f};
        float playrate = eng->m_par_playrate;
        float pitch = eng->m_par_pitch;
        float separ = rescale(pitch,-24.0f,24.0f,0.0f,1.0f);
        eng->m_eng->m_scrubber->setSeparation(separ);
        float loopstart = eng->m_par_regionselect;
        float looplen = 1.0f;
        float loopslide = 0.0f;
        float posrand = eng->m_par_srcposrand;
        float grate = std::pow(2.0f,1.0f/12*(12.0f*eng->m_par_grainrate));
        float lenm = eng->m_par_lenmultip;
        float revprob = eng->m_par_reverseprob;
        float pitchspread = eng->m_par_pitchsrpead;
        eng->m_eng->m_scanpos = eng->m_par_scanpos;
        float* obuf = (float*)outputBuffer;
        float* inbuf = (float*)inputBuffer;
        float panspread = clamp(eng->m_par_stereo_spread,-1.0f,1.0f);
        
        for (int i=0;i<framesPerBuffer;++i)
        {
            float inputgain = eng->m_drywetsmoother.process(eng->m_par_inputmix);
            float procgain = 1.0f-inputgain;
            float ins[2] = {inbuf[i*2+0],inbuf[i*2+0]};
            eng->m_eng->process(deltatime,sr,procbuf,playrate,pitch,loopstart,looplen,loopslide,
                posrand,grate,lenm,revprob,0,pitchspread);
            // filter low frequency junk
            procbuf[0] = eng->m_dc_blockers[0].process(procbuf[0]);
            procbuf[1] = eng->m_dc_blockers[1].process(procbuf[1]);
            // saturate (would ideally need some oversampling for this...)
            procbuf[0] = std::tanh(procbuf[0]);
            procbuf[1] = std::tanh(procbuf[1]);
            float mid = procbuf[0]+procbuf[1];
            float side = procbuf[1]-procbuf[0];
            side *= panspread;  
            obuf[i*2+0] = (mid - side) * procgain + ins[0] * inputgain; 
            obuf[i*2+1] = (mid + side) * procgain + ins[0] * inputgain;
            if (rec_active)
            {
                drsrc->pushSamplesToRecordBuffer(ins,0.9f);
            }
        }
        
        return paContinue;
    }
    void setNextPlayMode()
    {
        int& st = m_eng->m_playmode;
        st = (st + 1) % 3;
        m_cur_playstate = st;
    }
    int m_cur_playstate = 0;
    std::atomic<float> m_par_playrate{1.0f};
    std::atomic<float> m_par_pitch{0.0f};
    std::atomic<float> m_par_srcposrand{0.0f};
    std::atomic<float> m_par_pitchsrpead{0.0f};
    std::atomic<float> m_par_scanpos{0.0f};
    std::atomic<float> m_par_lenmultip{0.5f};
    std::atomic<float> m_par_reverseprob{0.0f};
    std::atomic<float> m_par_stereo_spread{1.0f};
    std::atomic<float> m_par_grainrate{4.0f}; // "octaves", 0 is 1 Hz
    std::atomic<float> m_par_regionselect{0.0f};
    std::atomic<float> m_par_inputmix{0.0f};
    std::atomic<bool> m_toggle_record{false};
    std::atomic<bool> m_add_marker{false};
    int m_cbcount = 0;
    int m_shift_state = 0;
    int m_big_fader_state = 0;
    int m_big_fader_values[2] = {0,0};
    std::array<dsp::BiquadFilter,2> m_dc_blockers;
    dsp::BiquadFilter m_drywetsmoother;
};

void mymidicb( double /*timeStamp*/, std::vector<unsigned char> *message, void *userData )
{
    if (!message)
        return;
    AudioEngine* eng = (AudioEngine*)userData;
    auto& msg = *message;
    if (msg.size()!=3)
        return;
    auto cf = [](std::atomic<float>& par,float step,float minv,float maxv)
    {
        float temp = par + step;
        temp = clamp(temp,minv,maxv);
        par = temp;
    };
    if (msg[0] >= 176)
    {
        if (msg[1] == 112)
        {
            if (msg[2]>0)
                eng->m_shift_state = 1;
            else eng->m_shift_state = 0;
        }
        if (msg[1] == 113)
        {
            if (msg[2]>0)
                eng->setNextPlayMode();
        }
        if (msg[1] == 114 && msg[2]>0)
        {
            eng->m_toggle_record = true;
        }
        if (msg[1] == 115 && msg[2]>0)
        {
            eng->m_add_marker = true;
        }
        if (msg[1] == 72 && eng->m_big_fader_state == 0)
        {
            eng->m_big_fader_values[0] = msg[2];
            eng->m_big_fader_state = 1;
        }
        if (msg[1] == 73 && eng->m_big_fader_state == 1)
        {
            eng->m_big_fader_values[1] = msg[2];
            eng->m_big_fader_state = 2;
        }
        if (eng->m_big_fader_state == 2)
        {
            eng->m_big_fader_state = 0;
            int thevalue = eng->m_big_fader_values[0]*128+eng->m_big_fader_values[1];
            float bigfadernorm = 1.0f/16384*thevalue;
            //std::cout << "big fader norm pos " << bigfadernorm << "\n";
            bigfadernorm = clamp(bigfadernorm,0.0f,1.0f);
            if (eng->m_cur_playstate>0 && eng->m_shift_state == 0)
                eng->m_par_scanpos = clamp(bigfadernorm,0.0f,1.0f);
            if (eng->m_cur_playstate>0 && eng->m_shift_state == 1)
                eng->m_par_regionselect = clamp(bigfadernorm,0.0f,1.0f);
            if (eng->m_cur_playstate==0 && eng->m_shift_state == 0)
                eng->m_par_regionselect = clamp(bigfadernorm,0.0f,1.0f);
        }
        float norm = 1.0/127*msg[2];
        float delta = 0.0f;
        float stepsmall = 1.0f;
        float steplarge = 2.0f;
        if (msg[2]>=64)
        {
            if (msg[2]==127)
                delta = -stepsmall;
            else 
                delta = -steplarge;
        }
        if (msg[2]<64)
        {
            if (msg[2]==1)
                delta = stepsmall;
            else delta = steplarge;
        }
            

        if (msg[1] == 64)
            cf(eng->m_par_playrate,delta*0.01f,-2.0f,2.0f);
        else if (msg[1] == 65 && eng->m_shift_state == 0)
            cf(eng->m_par_pitch,delta*0.1f,-24.0f,24.0f);
        else if (msg[1] == 65 && eng->m_shift_state == 1)
            cf(eng->m_par_pitchsrpead,delta*0.01f,-1.0f,1.0f);
        else if (msg[1] == 66)
            cf(eng->m_par_srcposrand,delta*0.01,0.0f,1.0f);
        else if (msg[1] == 67)
            cf(eng->m_par_lenmultip,delta*0.01,0.0f,1.0f);
        else if (msg[1] == 68)
            cf(eng->m_par_grainrate,delta*0.02f,-1.0f,7.0f);
        else if (msg[1] == 69)
            cf(eng->m_par_reverseprob,delta*0.01f,0.0f,1.0f);
        else if (msg[1] == 70)
            cf(eng->m_par_stereo_spread,delta*0.02f,-1.0f,1.0f);
        else if (msg[1] == 71)
            cf(eng->m_par_inputmix,delta*0.05f,0.0f,1.0f);
    }

}

inline bool findStringIC(const std::string & strSource, const std::string & strToFind)
{
  auto it = std::search(
    strSource.begin(), strSource.end(),
    strToFind.begin(),   strToFind.end(),
    [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
  );
  return (it != strSource.end() );
}

int main(int argc, char** argv)
{
    std::cout << "STARTING HEADLESS GRLOOPER\n";
    GrainEngine ge;
    ge.m_playmode = 0;
    std::unique_ptr<RtMidiIn> midi_input(new RtMidiIn);
    
    int incount = midi_input->getPortCount();
    int idx = -1;
    for (int i=0;i<incount;++i)
    {
        if (findStringIC(midi_input->getPortName(i),"noctu"))
        {
            idx = i;
            break;
        }
    }
    if (idx>=0)
    {
        std::cout << "using " << midi_input->getPortName(idx) << "\n";
        midi_input->openPort(idx);
    }
        
    else std::cout << "could not find nocturn\n";
    
    auto drsrc = dynamic_cast<DrWavSource*>(ge.m_srcs[0].get());
    if (drsrc->importFile("/home/teemu/AudioStuff/NilsVanOttorloo44/bassClarinet1.wav"))
    {
        std::cout << "loaded test source file\n";
    } else
    {
        std::cout << "could not load test file\n";
        return 1;
    }
    //ge.addEquidistantMarkers(8);
    AudioEngine aeng(&ge);
    midi_input->setCallback(mymidicb,(void*)&aeng);
    auto cf = [](char c, char incc, char decc, std::atomic<float>& par, float step)
    {
        if (c == incc)
            par = par + step;
        if (c == decc)
            par = par - step;
    };
    while (true)
    {
        char c = mygetch();
        if (c == 'q')
            break;
        cf(c,'a','A', aeng.m_par_playrate,0.05f);
        cf(c,'s','S', aeng.m_par_pitch,0.5f);
        if (c=='i')
        {
            auto& mo = ge.m_gm->m_interpmode;
            if (mo == 0)
                mo = 1;
            else mo = 0;
        }
        Pa_Sleep(20);
    }
    return 0;
}
#endif
