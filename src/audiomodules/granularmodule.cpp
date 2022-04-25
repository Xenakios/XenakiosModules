//#include "plugin.hpp"
#include "grain_engine.h"
#include "helperwidgets.h"
#include <osdialog.h>
#include <thread>
#include <mutex>
#include "dr_wav.h"

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
    std::mutex m_mut;
    std::atomic<int> m_do_update_peaks{0};
    std::string m_filename;
    void normalize(float level)
    {
        /*
        float peak = 0.0f;
        auto framesToUse = m_totalPCMFrameCount;
        int chanstouse = m_channels;
        float* dataToUse = m_audioBuffer.data();
        if (m_recordState>0)
        {
            framesToUse = m_audioBuffer.size();
            chanstouse = 1;
            dataToUse = m_audioBuffer.data();
        }
            
        for (int i=0;i<framesToUse*chanstouse;++i)
        {
            float s = std::fabs(dataToUse[i]);
            peak = std::max(s,peak);
        }
        float normfactor = 1.0f;
        if (peak>0.0f)
            normfactor = level/peak;
        for (int i=0;i<framesToUse*chanstouse;++i)
            dataToUse[i]*=normfactor;
        updatePeaks();
        */
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
    void updatePeaks()
    {
        if (m_do_update_peaks == 0)
            return;
        m_peak_updates_counter++;
        std::lock_guard<std::mutex> locker(m_peaks_mut);
        float* dataPtr = m_audioBuffer.data();
        peaksData.resize(m_channels);
        int samplesPerPeak = 32;
        int numPeaks = m_totalPCMFrameCount/samplesPerPeak;
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
    void putIntoBuffer(float* dest, int frames, int channels, int startInSource) override
    {
        std::lock_guard<std::mutex> locker(m_mut);
        float* srcDataPtr = m_audioBuffer.data();
        
        if (m_channels==0)
        {
            for (int i=0;i<frames*channels;++i)
                dest[i]=0.0f;
            return;
        }
        const int srcchanmap[4][4]=
        {
            {0,0,0,0},
            {0,1,0,1},
            {0,1,2,0},
            {0,1,2,3}
        };
        for (int i=0;i<frames;++i)
        {
            int index = i+startInSource;
            if (index>=0 && index<m_totalPCMFrameCount)
            {
                for (int j=0;j<channels;++j)
                {
                    int actsrcchan = srcchanmap[m_channels-1][j];
                    dest[i*channels+j] = srcDataPtr[index*m_channels+actsrcchan];
                }
            } else
            {
                for (int j=0;j<channels;++j)
                {
                    dest[i*channels+j] = 0.0f;
                }
            }
        }
    }
    ~DrWavSource()
    {
        if (m_has_recorded)
        {
            std::string audioDir = rack::asset::user("XenakiosGrainAudioFiles");
            uint64_t t = system::getUnixTime();
            std::string audioFile = audioDir+"/GrainRec_"+std::to_string(t)+".wav";
            saveFile(audioFile);
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

class GrainEngine
{
public:
    GrainEngine()
    {
        m_srcs.emplace_back(new DrWavSource);
        m_gm.reset(new GrainMixer(m_srcs));
        m_markers.reserve(1000);
        //for (int i=0;i<17;++i)
        //    m_markers.push_back(1.0f/16*i);
        //m_markers.erase(m_markers.begin()+5);
        m_markers = {0.0f,1.0f};
    }
    bool isRecording()
    {
        auto src = dynamic_cast<DrWavSource*>(m_srcs[0].get());
        return src->m_recordState>0;
    }
    std::vector<float> m_markers;
    void addMarker()
    {
        float insr = m_gm->m_sources[0]->getSourceSampleRate();
        float inlensamps = m_gm->m_sources[0]->getSourceNumSamples();
        float inlensecs = insr * inlensamps;
        float tpos = 1.0f/inlensamps*m_gm->m_srcpos;
        tpos += m_gm->m_loopstart;
        tpos = clamp(tpos,0.0f,1.0f);
        m_markers.push_back(tpos);
        std::sort(m_markers.begin(),m_markers.end());
        m_gm->m_srcpos = 0.0f;
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
        float posrand, float grate, float lenm, float revprob, int ss)
    {
        buf[0] = 0.0f;
        buf[1] = 0.0f;
        buf[2] = 0.0f;
        buf[3] = 0.0f;
        m_gm->m_sr = sr;
        m_gm->m_inputdur = m_srcs[0]->getSourceNumSamples();
        int markerIndex = (m_markers.size()-1)*loopstart;
        markerIndex = clamp(markerIndex,0,m_markers.size()-2);
        float regionStart = m_markers[markerIndex];
        m_gm->m_loopstart = regionStart;
        ++markerIndex;
        float regionEnd = m_markers[markerIndex];
        float regionLen = regionEnd - regionStart; 
        regionLen = clamp(regionLen,0.0f,1.0f);
        m_gm->m_looplen = looplen * regionLen;
        m_gm->m_loopslide = loopslide;
        m_gm->m_sourcePlaySpeed = playrate;
        m_gm->m_pitch = pitch;
        m_gm->m_posrandamt = posrand;
        m_gm->m_reverseProb = revprob;
        m_gm->setDensity(grate);
        m_gm->setLengthMultiplier(lenm);
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
        return resultJ;
    }
    void dataFromJson(json_t* root) 
    {
        if (!root)
            return;
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
private:
    
};

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
        PAR_LAST
    };
    enum OUTPUTS
    {
        OUT_AUDIO,
        OUT_LOOP_EOC,
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
        IN_LAST
    };
    dsp::BooleanTrigger m_recordTrigger;
    //bool m_recordActive = false;
    dsp::BooleanTrigger m_insertMarkerTrigger;
    dsp::BooleanTrigger m_resetTrigger;
    dsp::SchmittTrigger m_resetInTrigger;
    XGranularModule()
    {
        std::string audioDir = rack::asset::user("XenakiosGrainAudioFiles");
        rack::system::createDirectory(audioDir);
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_PLAYRATE,-1.0f,1.0f,0.5f,"Playrate");
        configParam(PAR_PITCH,-24.0f,24.0f,0.0f,"Pitch");
        configParam(PAR_LOOPSELECT,0.0f,1.0f,0.0f,"Region select");
        configParam(PAR_LOOPLEN,0.0f,1.0f,1.0f,"Loop length");
        configParam(PAR_ATTN_PLAYRATE,-1.0f,1.0f,0.0f,"Playrate CV ATTN");
        configParam(PAR_ATTN_PITCH,-1.0f,1.0f,0.0f,"Pitch CV ATTN");
        configParam(PAR_SRCPOSRANDOM,0.0f,1.0f,0.0f,"Source position randomization");
        configParam(PAR_ATTN_LOOPSTART,-1.0f,1.0f,0.0f,"Loop start CV ATTN");
        configParam(PAR_ATTN_LOOPLEN,-1.0f,1.0f,0.0f,"Loop len CV ATTN");
        configParam(PAR_GRAINDENSITY,0.0f,1.0f,0.25f,"Grain rate");
        configParam(PAR_RECORD_ACTIVE,0.0f,1.0f,0.0f,"Record active");
        configParam(PAR_LEN_MULTIP,0.0f,1.0f,0.25f,"Grain length");
        configParam(PAR_REVERSE,0.0f,1.0f,0.0f,"Grain reverse probability");
        configParam(PAR_SOURCESELECT,0.0f,7.0f,0.0f,"Source select");
        configParam(PAR_INPUT_MIX,0.0f,1.0f,0.0f,"Input mix");
        configParam(PAR_INSERT_MARKER,0.0f,1.0f,0.0f,"Insert marker");
        configParam(PAR_LOOP_SLIDE,0.0f,1.0f,0.0f,"Loop slide");
        configParam(PAR_RESET,0.0f,1.0f,0.0f,"Reset");
    }
    json_t* dataToJson() override
    {
        json_t* resultJ = json_object();
        json_object_set(resultJ,"importedfile",json_string(m_currentFile.c_str()));
        auto markersJ = m_eng.dataToJson();
        json_object_set(resultJ,"markers",markersJ);
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
    void process(const ProcessArgs& args) override
    {
        float prate = params[PAR_PLAYRATE].getValue();
        prate = getNotchedPlayRate(prate);
        m_notched_rate = prate;
        prate += inputs[IN_CV_PLAYRATE].getVoltage()*params[PAR_ATTN_PLAYRATE].getValue()*0.2f;
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
        float pitch = params[PAR_PITCH].getValue();
        pitch += inputs[IN_CV_PITCH].getVoltage()*12.0f;
        pitch = clamp(pitch,-24.0f,24.0f);
        float loopstart = params[PAR_LOOPSELECT].getValue();
        loopstart += inputs[IN_CV_LOOPSTART].getVoltage()*params[PAR_ATTN_LOOPSTART].getValue()/10.0f;
        loopstart = clamp(loopstart,0.0f,1.0f);
        float looplen = params[PAR_LOOPLEN].getValue();
        looplen += inputs[IN_CV_LOOPLEN].getVoltage()*params[PAR_ATTN_LOOPLEN].getValue()/10.0f;
        looplen = clamp(looplen,0.0f,1.0f);
        float posrnd = params[PAR_SRCPOSRANDOM].getValue();
        float grate = params[PAR_GRAINDENSITY].getValue();
        grate += inputs[IN_CV_GRAINRATE].getVoltage()*0.2f;
        grate = clamp(grate,0.0f,1.0f);
        grate = 0.01f+std::pow(1.0f-grate,2.0f)*0.49;
        float glenm = params[PAR_LEN_MULTIP].getValue();
        float revprob = params[PAR_REVERSE].getValue();
        auto drsrc = dynamic_cast<DrWavSource*>(m_eng.m_srcs[0].get());
        if (m_recordTrigger.process(params[PAR_RECORD_ACTIVE].getValue()>0.5f))
        {
            
            if (m_eng.isRecording() == false)
            {
                drsrc->startRecording(2,args.sampleRate);
                m_eng.addMarkerAtPosition(drsrc->getRecordPosition());
            }
            else
            {
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
        m_eng.process(args.sampleTime, args.sampleRate, buf,prate,pitch,loopstart,looplen,loopslide,
            posrnd,grate,glenm,revprob, srcindex);
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
        if (m_insertMarkerTrigger.process(params[PAR_INSERT_MARKER].getValue()>0.5f))
        {
            m_eng.addMarker();
        }

        if (m_next_marker_action == ACT_CLEAR_ALL_MARKERS)
        {
            m_next_marker_action = ACT_NONE;
            m_eng.clearMarkers();
        }
        if (m_next_marker_action == ACT_RESET_RECORD_HEAD)
        {
            drsrc->resetRecording();
            m_next_marker_action = ACT_NONE;
        }
        if (m_next_marker_action == ACT_CLEAR_ALL_AUDIO)
        {
            drsrc->clearAudio(-1,-1);
            m_next_marker_action = ACT_NONE;
        }
        if (m_next_marker_action == ACT_CLEAR_REGION)
        {
            int startSample = m_eng.m_gm->m_loopstart * m_eng.m_gm->m_inputdur;
            int endSample = startSample + (m_eng.m_gm->m_looplen * m_eng.m_gm->m_inputdur);
            drsrc->clearAudio(startSample,endSample);
            m_next_marker_action = ACT_NONE;
        }
        graindebugcounter = m_eng.m_gm->debugCounter;
    }
    int graindebugcounter = 0;
    enum ACTIONS
    {
        ACT_NONE,
        ACT_CLEAR_ALL_MARKERS,
        ACT_RESET_RECORD_HEAD,
        ACT_CLEAR_ALL_AUDIO,
        ACT_CLEAR_REGION,
        ACT_LAST
    };
    std::atomic<int> m_next_marker_action{ACT_NONE};
    GrainEngine m_eng;
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
        nvgStrokeColor(args.vg,nvgRGBA(0xff, 0xff, 0xff, 0xff));
        auto& src = *dynamic_cast<DrWavSource*>(m_gm->m_eng.m_srcs[0].get());
        if (src.m_channels>0)
        {
            std::lock_guard<std::mutex> locker(src.m_peaks_mut);
            int numpeaks = box.size.x - 2;
            int numchans = src.m_channels;
            float numsrcpeaks = src.peaksData[0].size();
            float loopstartnorm = m_gm->m_eng.m_gm->m_loopstart;
            float loopendnorm = loopstartnorm + m_gm->m_eng.m_gm->m_looplen;
            float startpeaks = 0.0f ;
            float endpeaks = numsrcpeaks ;
            if (m_opts == 1)
            {
                startpeaks = loopstartnorm * numsrcpeaks ;
                endpeaks = loopendnorm * numsrcpeaks ;
            }
            float chanh = box.size.y/numchans;
            nvgBeginPath(args.vg);
            nvgStrokeWidth(args.vg,1.5f);
            for (int i=0;i<numchans;++i)
            {
                for (int j=0;j<numpeaks;++j)
                {
                    float index = rescale(j,0,numpeaks,startpeaks,endpeaks-1.0f);
                    if (index>=0.0f && index<numsrcpeaks)
                    {
                        int index_i = index;
                        float minp = src.peaksData[i][index_i].minpeak;
                        float maxp = src.peaksData[i][index_i].maxpeak;
                        float ycor0 = rescale(minp,-1.0f,1.0,0.0f,chanh);
                        float ycor1 = rescale(maxp,-1.0f,1.0,0.0f,chanh);
                        nvgMoveTo(args.vg,j,chanh*i+ycor0);
                        nvgLineTo(args.vg,j,chanh*i+ycor1);
                    }
                    
                }
            }
            nvgStroke(args.vg);
            if (m_opts == 1)
            {
                nvgBeginPath(args.vg);
                nvgStrokeColor(args.vg,nvgRGBA(0xff, 0xff, 0xff, 0xff));
                for (int i=0;i<16;++i)
                {
                    float ppos = m_gm->m_eng.m_gm->getGrainSourcePosition(i);
                    if (ppos>=0.0f)
                    {
                        float srcdur = m_gm->m_eng.m_gm->m_inputdur;
                        float xcor = rescale(ppos,loopstartnorm,loopendnorm,0.0f,box.size.x);
                        float ycor0 = box.size.y / 16 * i;
                        float ycor1 = box.size.y / 16*(i+1);
                        nvgMoveTo(args.vg,xcor,ycor0);
                        nvgLineTo(args.vg,xcor,ycor1);
                    }
                    
                }
                
                nvgStroke(args.vg);
            }
            if (m_opts == 0)
            {
                nvgBeginPath(args.vg);
                nvgStrokeColor(args.vg,nvgRGBA(0x00, 0xff, 0xff, 0xff));
                auto& markers = m_gm->m_eng.m_markers; 
                for (int i=0;i<markers.size();++i)
                {
                    float xcor = rescale(markers[i],0.0f,1.0f,0.0f,box.size.x);
                    nvgMoveTo(args.vg,xcor,box.size.y-5.0f);
                    nvgLineTo(args.vg,xcor,box.size.y);
                }
                nvgStroke(args.vg);
                
                nvgBeginPath(args.vg);
                nvgFillColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0x80));
                float loopstart = m_gm->m_eng.m_gm->m_actLoopstart;
                float loopend = m_gm->m_eng.m_gm->m_actLoopend;
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
            }
        }
        nvgRestore(args.vg);
    }
};

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
        auto normItem = createMenuItem([this,drsrc](){ drsrc->normalize(1.0f); },"Normalize buffer");
        menu->addChild(normItem);
        auto revItem = createMenuItem([this,drsrc](){ drsrc->reverse(); },"Reverse buffer");
        menu->addChild(revItem);
        auto clearmarksItem = createMenuItem([this]()
        { m_gm->m_next_marker_action = XGranularModule::ACT_CLEAR_ALL_MARKERS; },"Clear all markers");
        menu->addChild(clearmarksItem);
        auto resetrec = createMenuItem([this]()
        { m_gm->m_next_marker_action = XGranularModule::ACT_RESET_RECORD_HEAD; },"Reset record state");
        menu->addChild(resetrec);
        auto clearall = createMenuItem([this]()
        { m_gm->m_next_marker_action = XGranularModule::ACT_CLEAR_ALL_AUDIO; },"Clear all audio");
        menu->addChild(clearall);
        auto clearregion = createMenuItem([this]()
        { m_gm->m_next_marker_action = XGranularModule::ACT_CLEAR_REGION; },"Clear region audio");
        menu->addChild(clearregion);
    }
    XGranularWidget(XGranularModule* m)
    {
        setModule(m);
        m_gm = m;
        box.size.x = RACK_GRID_WIDTH*21;
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "GRAINS",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        
        auto port = new PortWithBackGround(m,this,XGranularModule::OUT_AUDIO,1,17,"AUDIO OUT 1",true);
        port = new PortWithBackGround(m,this,XGranularModule::OUT_LOOP_EOC,92,17,"LOOP EOC",true);
        port = new PortWithBackGround(m,this,XGranularModule::IN_AUDIO,34,17,"AUDIO IN",false);
        
        addParam(createParam<TL1105>(Vec(62,34),m,XGranularModule::PAR_RECORD_ACTIVE));
        addParam(createParam<TL1105>(Vec(150,34),m,XGranularModule::PAR_INSERT_MARKER));
        addParam(createParam<TL1105>(Vec(180,34),m,XGranularModule::PAR_RESET));
        port = new PortWithBackGround(m,this,XGranularModule::IN_RESET,180,17,"RST",false);
        addParam(createParam<Trimpot>(Vec(62,14),m,XGranularModule::PAR_INPUT_MIX));
        addChild(new KnobInAttnWidget(this,
            "PLAYRATE",XGranularModule::PAR_PLAYRATE,
            XGranularModule::IN_CV_PLAYRATE,XGranularModule::PAR_ATTN_PLAYRATE,1.0f,60.0f));
        addChild(new KnobInAttnWidget(this,
            "PITCH",XGranularModule::PAR_PITCH,XGranularModule::IN_CV_PITCH,-1,82.0f,60.0f));
        addChild(new KnobInAttnWidget(this,
            "LOOP SLIDE",XGranularModule::PAR_LOOP_SLIDE,-1,-1,2*82.0f,60.0f));
        addChild(new KnobInAttnWidget(this,"REGION SELECT",
            XGranularModule::PAR_LOOPSELECT,XGranularModule::IN_CV_LOOPSTART,XGranularModule::PAR_ATTN_LOOPSTART,1.0f,101.0f));
        addChild(new KnobInAttnWidget(this,"LOOP LENGTH",
            XGranularModule::PAR_LOOPLEN,XGranularModule::IN_CV_LOOPLEN,XGranularModule::PAR_ATTN_LOOPLEN,82.0f,101.0f));
        addChild(new KnobInAttnWidget(this,"SOURCE POS RAND",XGranularModule::PAR_SRCPOSRANDOM,-1,-1,1.0f,142.0f));
        addChild(new KnobInAttnWidget(this,"GRAIN RATE",XGranularModule::PAR_GRAINDENSITY,XGranularModule::IN_CV_GRAINRATE,-1,82.0f,142.0f));
        addChild(new KnobInAttnWidget(this,"GRAIN LEN",XGranularModule::PAR_LEN_MULTIP,-1,-1,2*82.0f,142.0f));
        addChild(new KnobInAttnWidget(this,"GRAIN REVERSE",XGranularModule::PAR_REVERSE,-1,-1,2*82.0f,101.0f));
        WaveFormWidget* wavew = new WaveFormWidget(m,0);
        wavew->box.pos = {1.0f,215.0f};
        wavew->box.size = {box.size.x-2.0f,50.0f};
        addChild(wavew); 
        wavew = new WaveFormWidget(m,1);
        wavew->box.pos = {1.0f,265.0f};
        wavew->box.size = {box.size.x-2.0f,110.0f};
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
            char buf[100];
            auto& src = *dynamic_cast<DrWavSource*>(m_gm->m_eng.m_srcs[0].get());
            std::string rectext;
            if (m_gm->m_eng.isRecording())
                rectext = "REC";
            sprintf(buf,"%d %d %f %s %d",
                m_gm->graindebugcounter,m_gm->m_eng.m_gm->m_grainsUsed,m_gm->m_notched_rate,
                rectext.c_str(),src.m_peak_updates_counter);
            nvgFontSize(args.vg, 15);
            nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            
            nvgText(args.vg, 1 , 215, buf, NULL);

        }
        


        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelXGranular = createModel<XGranularModule, XGranularWidget>("XGranular");
