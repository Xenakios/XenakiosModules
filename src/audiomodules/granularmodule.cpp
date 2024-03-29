#include "grain_engine.h"
#ifndef RAPIHEADLESS
#include "helperwidgets.h"
#include <osdialog.h>
#else
#include <system.hpp>
#include <jansson.h>
#include <sys/stat.h>
#include <iostream>
#include <mutex>
#include <functional>
#include <string.hpp>
#include <iomanip>
#include "sndfile.h"
#include "choc_AudioFileFormat.h"

#endif
#include <iostream>
#include <thread>
#include <mutex>
#include "dr_wav.h"
#include "choc_SingleReaderSingleWriterFIFO.h"

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
        auto drsrc = dynamic_cast<MultiBufferSource*>(m_eng.m_srcs[0].get());
        if (drsrc && drsrc->importFile(filename,0))
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
        auto drsrc = dynamic_cast<MultiBufferSource*>(m_eng.m_srcs[0].get());
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
        auto drsrc = dynamic_cast<MultiBufferSource*>(m_eng.m_srcs[0].get());
        int startSample = m_eng.m_gm->m_region_start * m_eng.m_gm->m_inputdur;
        int endSample = startSample + (m_eng.m_gm->m_region_len * m_eng.m_gm->m_inputdur);
        drsrc->clearAudio(startSample,endSample,0);
    }
    int graindebugcounter = 0;
    void normalizeAudio(int opts, float peakgain)
    {
        auto drsrc = dynamic_cast<MultiBufferSource*>(m_eng.m_srcs[0].get());
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

        auto& src = *dynamic_cast<MultiBufferSource*>(m_gm->m_eng.m_srcs[0].get());
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
        auto drsrc = dynamic_cast<MultiBufferSource*>(m_gm->m_eng.m_srcs[0].get());
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
                    drsrc->clearAudio(-1,-1,0);
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
            auto& src = *dynamic_cast<MultiBufferSource*>(m_gm->m_eng.m_srcs[0].get());
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
            auto& src = *dynamic_cast<MultiBufferSource*>(m_gm->m_eng.m_srcs[0].get());
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
#include <type_traits>
#include "claphost.h"
#include <ncurses.h>
#include "nocturncontrol.h"

class AudioEngine
{
public:
    std::unique_ptr<clap_processor> m_clap_host;
    GrainEngine* m_eng = nullptr;
    PaStreamParameters outputParameters;
    PaStreamParameters inputParameters;
    PaStream *stream = nullptr;
    bool pa_inited = false;
    
    AudioEngine(GrainEngine* e) : m_eng(e)
    {
        exFIFO.reset(64);
        m_cur_playstate = m_eng->m_playmode;
        std::cout << "attempting to start portaudio\n";
        m_dc_blockers[0].setParameters(rack::dsp::BiquadFilter::HIGHPASS_1POLE,30.0f/44100.0,1.0,1.0f);
        m_dc_blockers[1].setParameters(rack::dsp::BiquadFilter::HIGHPASS_1POLE,30.0f/44100.0,1.0,1.0f);
        m_drywetsmoother.setParameters(rack::dsp::BiquadFilter::LOWPASS_1POLE,10.0f/44100.0,1.0,1.0f);
        m_wsmorphsmoother.setParameters(rack::dsp::BiquadFilter::LOWPASS_1POLE,10.0f/44100.0,1.0,1.0f);
        m_mastergainsmoother.setParameters(rack::dsp::BiquadFilter::LOWPASS_1POLE,10.0f/44100.0,1.0,1.0f);
        PaError err;
        err = Pa_Initialize();
        if (err == paNoError)
            pa_inited = true;
        else return;
        printError(err);
        for (int i=0;i<Pa_GetDeviceCount();++i)
        {
            std::cout << i << " :: " << (*Pa_GetDeviceInfo(i)).name << "\n";
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
        clap_info_item item{"/home/pi/.clap/Surge XT Effects.clap","",0};
        m_clap_host = std::make_unique<clap_processor>(item);
        m_clap_host->exFIFO = &exFIFO;
        m_cpu_smoother.setRiseFall(1.0f,1.0f);
    }
    void printError(PaError e)
    {
        if (e == paNoError)
            return;
        fprintf( stderr, "An error occured while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", e );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( e ) );
    }
    virtual ~AudioEngine()
    {
        std::cout << "finishing portaudio\n";
        if (stream)
        {
            Pa_StopStream(stream);
            Pa_CloseStream( stream );
        }
        if (pa_inited)
            Pa_Terminate();
        
    }
    float waveShape(float in, float morph)
    {
        morph *= 3.0f;
        float outs[4]; // = {0.0f,0.0f,0.0f,0.0f};
        outs[0] = std::tanh(in);
        outs[1] = clamp(in,-1.0f,1.0f);
        float drive = 1.0f;
        if (morph>=2.0f)
            drive = rescale(morph,2.0f,3.0f,1.0f,10.0f);
        outs[2] = std::atan(in*drive)*2.0f/g_pi;
        outs[3] = outs[2]; // guard point
        return interpolateLinear(outs,morph);
    }
    std::atomic<bool> m_rec_active{false};
    virtual int processBlock(const float* inputBuffer, float* outputBuffer, int nFrames)
    {
        float sr = 44100.0f;
        auto drsrc = dynamic_cast<MultiBufferSource*>(m_eng->m_srcs[0].get());
        // can always play any reel
        drsrc->setPlaybackBufferIndex(m_active_reel);
        bool rec_active = false;
        // but only reels 1-3 can record input
        if (m_active_reel>0 && m_active_reel<4)
        {
            drsrc->setRecordBufferIndex(m_active_reel);
        
            rec_active = m_eng->isRecording();
            if (m_next_record_action == 1)
            {
                if (rec_active == false)
                {
                    drsrc->startRecording(2,sr);
                    rec_active = true;
                } else
                {
                    float rpos = drsrc->getRecordPosition();
                    m_eng->addMarkerAtPosition(rpos);
                    drsrc->stopRecording();
                    rec_active = false;
                }
                m_next_record_action = 0;
            }
            if (m_next_record_action == 2)
            {
                if (rec_active == false)
                {
                    int curplaypos = m_eng->m_gm->getSourcePlayPosition();
                    auto range = m_eng->getActiveRegionRange();
                    int minpos = range.first * m_eng->m_gm->m_inputdur;
                    int maxpos = range.second * m_eng->m_gm->m_inputdur;
                    drsrc->startOverDubbing(2,sr,curplaypos,minpos,maxpos);
                    rec_active = true;
                } else
                {
                    //float rpos = drsrc->getRecordPosition();
                    //m_eng->addMarkerAtPosition(rpos);
                    drsrc->stopRecording();
                    rec_active = false;
                }
                m_next_record_action = 0;
            }
        } else
        {
            m_next_record_action = 0;
        }
        if (m_next_marker_act == 1)
        {
            m_next_marker_act = 0;
            m_eng->addMarker();
            dumpMarkers();
        }
        if (m_next_marker_act == 2)
        {
            m_next_marker_act = 0;
            m_eng->clearMarkers();
            dumpMarkers();
        }
        float deltatime = 1.0f/sr;
        float procbuf[4] = {0.0f,0.0f,0.0f,0.0f};
        float playrate = m_par_playrate;
        float pitch = m_par_pitch;
        float separ = rescale(pitch,-24.0f,24.0f,0.0f,1.0f);
        m_eng->m_scrubber->setSeparation(separ);
        float loopstart = m_par_regionselect;
        float looplen = 1.0f;
        float loopslide = 0.0f;
        float posrand = std::pow(m_par_srcposrand,2.0f);
        float grate = std::pow(2.0f,1.0f/12*(12.0f*m_par_grainrate));
        float lenm = m_par_lenmultip;
        float revprob = m_par_reverseprob;
        float pitchspread = m_par_pitchsrpead;
        m_eng->m_scanpos = m_par_scanpos;
        
        float panspread = clamp(m_par_stereo_spread,-1.0f,1.0f);
        float mastergain = 1.0f;
        // Basic output volume compensation method. 
        // Assume more grains mixed are louder and out volume needs to be attenuated.
        // In practice a more sophisticated way to calculate this should probably be figured out. 
        int gused = m_eng->m_gm->m_grainsUsed;
        if (m_eng->m_playmode < 2 && gused>0) // if in scrub mode, use unity gain
            mastergain = clamp(1.0f/gused,0.0f,0.70f);
        
        for (int i=0;i<nFrames;++i)
        {
            float inputgain = m_drywetsmoother.process(m_par_inputmix);
            float procgain = 1.0f-inputgain;
            float ins[2] = {inputBuffer[i*2+0],inputBuffer[i*2+0]};
            m_eng->process(deltatime,sr,procbuf,playrate,pitch,loopstart,looplen,loopslide,
                posrand,grate,lenm,revprob,0,pitchspread);
            // filter low frequency junk
            procbuf[0] = m_dc_blockers[0].process(procbuf[0]);
            procbuf[1] = m_dc_blockers[1].process(procbuf[1]);
            float outgain = m_mastergainsmoother.process(mastergain);
            // clip/saturate (would ideally need some oversampling for this...)
            float morpha = m_wsmorphsmoother.process(m_par_waveshapemorph);
            procbuf[0] = waveShape(procbuf[0]*outgain,1.0f);
            procbuf[1] = waveShape(procbuf[1]*outgain,1.0f);
            float mid = 0.5f*(procbuf[0]+procbuf[1]);
            float side = 0.5f*(procbuf[1]-procbuf[0]);
            side *= panspread;  
            procbuf[0] = (mid-side);
            procbuf[1] = (mid+side);
            if (m_out_record_active == 1)
            {
                drsrc->pushSamplesToRecordBuffer(procbuf,1.0f,4,true);
            }
                
            
            outputBuffer[i*2+0] = procbuf[0] * procgain + ins[0] * inputgain; 
            outputBuffer[i*2+1] = procbuf[1] * procgain + ins[0] * inputgain;
            if (rec_active)
            {
                drsrc->pushSamplesToRecordBuffer(ins,0.9f);
            }
        }
        m_clap_host->processAudio(outputBuffer,nFrames);
        return paContinue;
    }
    std::atomic<int> m_out_record_active{0};
    void dumpMarkers()
    {
        exFIFO.push([this]
        {
            for (auto& e : m_eng->m_markers)
                std::cout << e << " ";
            std::cout << "\n";
        });
    }
    static int paCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
    {
        AudioEngine* eng = (AudioEngine*)userData;
        memset(outputBuffer,0,sizeof(float)*2*framesPerBuffer); // not known yet if the output buffer is precleared
        ++eng->m_cbcount;
        if (eng->m_cbcount<100) // little hack to do no work at startup
        {
            return paContinue;
        }
        float* obuf = (float*)outputBuffer;
        const float* inbuf = (const float*)inputBuffer;
        return eng->processBlock(inbuf,obuf,framesPerBuffer);
        
    }
    std::atomic<float> m_recseconds{0.0f};
    
    void setNextPlayMode()
    {
        int& st = m_eng->m_playmode;
        st = (st + 1) % 3;
        m_cur_playstate = st;
    }
    void stepActiveReel(int step)
    {
        if (m_eng->isRecording())
        {
            return;
        }
            
        int temp = m_active_reel;
        temp += step;
        if (temp<0)
            temp = 4;
        if (temp>4)
            temp = 0;
        m_active_reel = temp;
    }
    void setPlayMode(int m)
    {
        m_eng->m_playmode = m;
        m_cur_playstate = m;
    }
    int m_cur_playstate = 0;
    std::atomic<float> m_par_playrate{1.0f};
    std::atomic<float> m_par_pitch{0.0f};
    std::atomic<float> m_par_srcposrand{0.0f};
    std::atomic<float> m_par_pitchsrpead{0.0f};
    std::atomic<float> m_par_scanpos{0.0f};
    std::atomic<float> m_par_lenmultip{0.75f};
    std::atomic<float> m_par_reverseprob{0.0f};
    std::atomic<float> m_par_stereo_spread{1.0f};
    std::atomic<float> m_par_grainrate{4.0f}; // "octaves", 0 is 1 Hz
    std::atomic<float> m_par_regionselect{0.0f};
    std::atomic<float> m_par_inputmix{0.0f};
    std::atomic<float> m_par_waveshapemorph{0.0f};
    std::atomic<int> m_next_record_action{0};
    std::atomic<int> m_next_marker_act{0};
    std::atomic<int> m_led_ring_option{0};
    std::atomic<int> m_active_reel{0};
    using par_pair = std::pair<std::string,std::atomic<float>*>;
    std::vector<par_pair> params={
            {"par_playrate",&m_par_playrate},
            {"par_pitch",&m_par_pitch},
            {"par_stereospread",&m_par_stereo_spread},
            {"par_sourceposspread",&m_par_srcposrand},
            {"par_pitchspread",&m_par_pitchsrpead},
            {"par_inputmix",&m_par_inputmix},
            {"par_scanpos",&m_par_scanpos},
            {"par_lenmultip",&m_par_lenmultip},
            {"par_reverseprob",&m_par_reverseprob},
            {"par_regionselect",&m_par_regionselect},
            {"par_grainrate",&m_par_grainrate},
            {"par_waveshapemorph",&m_par_waveshapemorph}
            };
    void stepPage(int step)
    {
        int temp = m_page_state;
        temp += step;
        if (temp>3)
            temp = 0;
        if (temp<0)
            temp = 3;
        m_page_state = temp;
    }
    json_t* dataToJson()
    {
        json_t* result = json_object();
        json_object_set(result,"plug0state",m_clap_host->dataToJson());
        return result;
    }
    std::string dataFromJson(json_t* j)
    {
        if (!j)
            return "no data";
        json_t* plugdata = json_object_get(j,"plug0state");
        auto err = m_clap_host->dataFromJson(plugdata);
        return err;
    }
    int m_cbcount = 0;
    /*
    0 : main grlooper parameters
    1 : aux grlooper parameters
    2 : fx params 0..7
    3 : fx params 8..11
    */
    std::atomic<int> m_page_state{0};
    std::atomic<int> m_shift_state{0};
    int m_big_fader_state = 0;
    int m_big_fader_values[2] = {0,0};
    std::array<dsp::BiquadFilter,2> m_dc_blockers;
    dsp::BiquadFilter m_drywetsmoother;
    dsp::BiquadFilter m_wsmorphsmoother;
    dsp::BiquadFilter m_mastergainsmoother;
    
    choc::fifo::SingleReaderSingleWriterFIFO<std::function<void(void)>> exFIFO;
    dsp::SlewLimiter m_cpu_smoother;
    float getSmoothedCPU_Usage()
    {
        auto usage = Pa_GetStreamCpuLoad(stream);
        return m_cpu_smoother.process(0.1f,usage);
    }

};

class GrlooperNocturnControl : public NocturnControlBase
{
public:
    GrlooperNocturnControl(AudioEngine& eng) : m_eng(eng)
    {

    }
    void onEncoder(int idx, int step) override
    {
        auto cf = [](std::atomic<float>& par,float step,float minv,float maxv)
        {
            float temp = par + step;
            temp = clamp(temp,minv,maxv);
            par = temp;
        };
        float delta = step;
        if (m_page < 2)
        {
            if (idx == 0)
                cf(m_eng.m_par_playrate,delta*0.02f,-2.0f,2.0f);
            else if (idx == 1 && m_page == 0)
                cf(m_eng.m_par_pitch,delta*0.1f,-24.0f,24.0f);
            else if (idx == 1 && m_page == 1)
                cf(m_eng.m_par_pitchsrpead,delta*0.01f,-1.0f,1.0f);
            else if (idx == 2)
                cf(m_eng.m_par_srcposrand,delta*0.01,0.0f,1.0f);
            else if (idx == 3)
                cf(m_eng.m_par_lenmultip,delta*0.01,0.0f,1.0f);
            else if (idx == 4)
                cf(m_eng.m_par_grainrate,delta*0.02f,-1.0f,7.0f);
            else if (idx == 5)
                cf(m_eng.m_par_reverseprob,delta*0.01f,0.0f,1.0f);
            else if (idx == 6)
                cf(m_eng.m_par_stereo_spread,delta*0.02f,-1.0f,1.0f);
            else if (idx == 7 && m_page == 0)
                cf(m_eng.m_par_inputmix,delta*0.01f,0.0f,1.0f);
            else if (idx == 7 && m_page == 1)
                cf(m_eng.m_par_waveshapemorph,delta*0.01f,0.0f,1.0f);
            
        }
        if (m_page>=2) // control fx params
        {
            int paramindex = idx;
            if (m_page == 3)
                paramindex += 8;
            m_eng.m_clap_host->incDecParameter(paramindex,delta);
        }
    }
    void onCrossFader(float val) override
    {
        float bigfadernorm = clamp(val,0.0f,1.0f);
        if (m_eng.m_cur_playstate>0 && m_shiftstate == 0)
            m_eng.m_par_scanpos = clamp(bigfadernorm,0.0f,1.0f);
        if (m_eng.m_cur_playstate>0 && m_shiftstate == 1)
            m_eng.m_par_regionselect = clamp(bigfadernorm,0.0f,1.0f);
        if (m_eng.m_cur_playstate==0 && m_shiftstate == 0)
            m_eng.m_par_regionselect = clamp(bigfadernorm,0.0f,1.0f);
    }
    void onStepPage(int step) override
    {
        int temp = m_page;
        temp += step;
        if (temp<0)
            temp = 3;
        if (temp>3)
            temp = 0;
        m_page = temp;
        m_eng.stepPage(step);
    }
    void onButton(int idx, bool down) override
    {
        if (idx == 1)
            m_eng.setNextPlayMode();
    }
    void onLearnButton() override
    {

    }
private:
    AudioEngine& m_eng;
    std::atomic<int> m_page{0};
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
        if (msg[1] == 112) // button 1
        {
            if (msg[2]>0)
                eng->m_shift_state = 1;
            else eng->m_shift_state = 0;
        }
        if (msg[1] == 122) // page - button
        {
            if (msg[2]>0)
                eng->stepPage(-1);
        }
        if (msg[1] == 123) // page + button
        {
            if (msg[2]>0)
                eng->stepPage(1);
        }

        if (msg[1] == 113) // button 2
        {
            if (msg[2]>0)
                eng->setNextPlayMode();
        }
        if (msg[1] == 114 && msg[2]>0 && eng->m_shift_state == 0) // button 3, record
        {
            eng->m_next_record_action = 1;
        }
        if (msg[1] == 114 && msg[2]>0 && eng->m_shift_state == 1) // button 3 with shift, overdub
        {
            eng->m_next_record_action = 2;
        }
        if (msg[1] == 115 && msg[2]>0) // button 4
        {
            eng->m_next_marker_act = 1;
        }
        if (eng->m_shift_state == 1 && msg[1] == 115 && msg[2]>0) // button 4 with shift
        {
            eng->m_next_marker_act = 2;
        }
        if (msg[1] == 120 && msg[2]>0) // "learn" button
        {
            int temp = eng->m_led_ring_option;
            temp = (temp + 1) % 2;
            eng->m_led_ring_option = temp;
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
        if (eng->m_page_state < 2)
        {
            if (msg[1] == 64)
                cf(eng->m_par_playrate,delta*0.02f,-2.0f,2.0f);
            else if (msg[1] == 65 && eng->m_page_state == 0)
                cf(eng->m_par_pitch,delta*0.1f,-24.0f,24.0f);
            else if (msg[1] == 65 && eng->m_page_state == 1)
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
            else if (msg[1] == 71 && eng->m_page_state == 0)
                cf(eng->m_par_inputmix,delta*0.01f,0.0f,1.0f);
            else if (msg[1] == 71 && eng->m_page_state == 1)
                cf(eng->m_par_waveshapemorph,delta*0.01f,0.0f,1.0f);
        }
        if (eng->m_page_state>=2) // control fx params
        {
            int paramindex = msg[1] - 64;
            if (eng->m_page_state == 3)
                paramindex += 8;
            eng->m_clap_host->incDecParameter(paramindex,delta);
        }
        
    }

}

void saveSettings(AudioEngine& aeng)
{
    auto root = json_object();
    json_object_set(root,"playmode",json_integer(aeng.m_cur_playstate));
    for (auto& e : aeng.params)
    {
        json_object_set(root,e.first.c_str(),json_real(e.second->load()));    
    }
    auto pluginj = aeng.dataToJson();
    json_object_set(root,"aengdata",pluginj);
    auto markers = aeng.m_eng->dataToJson();
    json_object_set(root,"markers_etc",markers);
    json_dump_file(root,"settings.json",JSON_INDENT(2));
    json_decref(root);
}

void loadSettings(AudioEngine& aeng)
{
    json_error_t jerr;
    auto rootj = json_load_file("settings.json",0,&jerr);
    if (rootj)
    {
        std::cout << "loaded json settings!\n";
        auto valJ = json_object_get(rootj,"playmode");
        if (valJ)
            aeng.setPlayMode(json_integer_value(valJ));
        
        for (auto& e : aeng.params)
        {
            auto pj = json_object_get(rootj,e.first.c_str());
            if (pj)
                e.second->store(json_real_value(pj));
        }
        json_t* aengdata = json_object_get(rootj,"aengdata");
        auto err = aeng.dataFromJson(aengdata);
        std::cout << err << "\n";
        auto markersJ = json_object_get(rootj,"markers_etc");
        if (markersJ)
            aeng.m_eng->dataFromJson(markersJ);
        json_decref(rootj);
    } else
        std::cout << "could not load json settings :-(\n";
}

class lambdacontainer
{
public:
    template<typename F>
    lambdacontainer(F&& f)
    {
        static_assert(sizeof(F)<=64,"too BIG lambda, make it smaller!!!!");
        static_assert (std::is_trivially_copyable<F>::value,"Passed in lambda must be trivially copyable");
        new (internalbuf) funccontainerimp<F>(f);
    }
    void operator()()
    {
        funccontainerbase* f = reinterpret_cast<funccontainerbase*>(internalbuf);
        f->call();
    }
private:
    struct funccontainerbase
    {
        virtual ~funccontainerbase() {}
        virtual void call();
    };
    template<typename F>
    struct funccontainerimp  : public funccontainerbase
    {
        funccontainerimp(F f) : thefunc(f) {}
        void call() override
        {
            thefunc();
        }
        F thefunc;
    };
    unsigned char internalbuf[64];
};

void worker_thread_func(RtMidiOut* midi_output, AudioEngine& aeng, std::atomic<bool>& quit_thread)
{
    std::cout << "starting worker thread\n";
    unsigned char midimsg[4]={176,81,64,0};
    midi_output->sendMessage(midimsg,3);
    while (!quit_thread)
    {
        if (aeng.m_led_ring_option == 1)
        {
            // show source position in nocturn speed dial LED ring
            float tpos = aeng.m_eng->m_gm->getSourcePlayPosition();
            float srclen = aeng.m_eng->m_gm->m_inputdur;
                
            tpos = rescale(tpos,0.0f,srclen,0.0f,127.0f);
            tpos = clamp(tpos,0.0f,127.0f);
            midimsg[0] = 176;
            midimsg[1] = 80;
            midimsg[2] = (unsigned char)tpos;
            midi_output->sendMessage(midimsg,3);
        } else
        {
            // show region number in LED ring
            float tpos = aeng.m_eng->getCurrentRegionAsPercentage();
            tpos = rescale(tpos,0.0f,1.0f,0.0f,127.0f);
            tpos = clamp(tpos,0.0f,127.0f);
            midimsg[0] = 176;
            midimsg[1] = 80;
            midimsg[2] = (unsigned char)tpos;
            midi_output->sendMessage(midimsg,3);
        }
        
        // show record status on button 3
        midimsg[1] = 114;
        midimsg[2] = (unsigned char)aeng.m_eng->isRecording();
        midi_output->sendMessage(midimsg,3);
        std::function<void(void)> func;
        while(aeng.exFIFO.pop(func))
        {
            if (func) func();
        }
        Pa_Sleep(50);
    }
    std::cout << "ended worker thread\n";
}

int main(int argc, char** argv)
{
    std::cout << "STARTING HEADLESS GRLOOPER\n";
    
    GrainEngine ge;
    ge.m_playmode = 0;
    std::unique_ptr<RtMidiIn> midi_input(new RtMidiIn);
    std::unique_ptr<RtMidiOut> midi_output(new RtMidiOut);
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
    else std::cout << "could not find nocturn input\n";
    idx = -1;
    for (int i=0;i<midi_output->getPortCount();++i)
        if (findStringIC(midi_output->getPortName(i),"noctu"))
        {
            idx = i;
            break;    
        }
    if (idx>=0)
    {
        std::cout << "using " << midi_output->getPortName(idx) << "\n";
        midi_output->openPort(idx);
    }
    else std::cout << "could not find nocturn output\n";
    std::atomic<bool> quit_thread{false};
    
    auto drsrc = dynamic_cast<MultiBufferSource*>(ge.m_srcs[0].get());
    for (int i=0;i<5;++i)
    {
        std::string ifn = "../reels/reel_"+std::to_string(i)+".wav";
        std::cout << "trying to load audio file " << ifn << "\n";
        if (drsrc->importFile(ifn,i))
        {
            std::cout << "success\n";
            
            if (i == 0)
            {
                auto markers = drsrc->m_media_cues[i];
                if (markers.size()>1)
                {
                    ge.m_markers.clear();
                    for (auto& mark : markers)
                    {
                        float tpos = 1.0f/drsrc->getSourceNumSamples()*mark;
                        ge.m_markers.push_back(tpos);
                    }
                }
                
            }
            
        } else
        {
            std::cout << "failed\n";
        }
    }
    auto clapplugs = clap_processor::scanClaps();
    AudioEngine aeng(&ge);
    loadSettings(aeng);
    
    std::thread worker_th([&aeng,&midi_output,&quit_thread]()
    {
        worker_thread_func(midi_output.get(),aeng,quit_thread);
    });
    GrlooperNocturnControl noct_contr(aeng);
    midi_input->setCallback(GrlooperNocturnControl::MidiCallback,(void*)&noct_contr);
    auto cf = [](char c, char incc, char decc, std::atomic<float>& par, float step)
    {
        if (c == incc)
            par = par + step;
        if (c == decc)
            par = par - step;
    };
    initscr();
    noecho();
    
    WINDOW *win = newwin(12,100,0,0);
    wtimeout(win,100);
    aeng.m_page_state = 0;
    while (true)
    {
        char c = wgetch(win);
        if (c == 'q')
            break;
        wclear(win);
        cf(c,'a','A', aeng.m_par_playrate,0.05f);
        cf(c,'s','S', aeng.m_par_pitch,0.5f);
        if (c=='i')
        {
            auto& mo = ge.m_gm->m_interpmode;
            if (mo == 0)
                mo = 1;
            else mo = 0;
        }
        mvwprintw(win,0,0,"Interpolation mode %d, PortAudio CPU load %.0f %%"
            ,ge.m_gm->m_interpmode,100.0f*aeng.getSmoothedCPU_Usage());
        if (c=='r')
        {
            aeng.m_next_record_action = 1;
        }
        if (c=='R')
        {
            aeng.m_next_record_action = 2;
        }
        if (c=='M')
        {
            aeng.m_next_marker_act = 2;
        }
        if (c=='p')
            aeng.stepPage(1);
        if (c=='l')
            aeng.stepActiveReel(1);
        if (aeng.m_page_state == 0)
        {
            mvwprintw(win,1,0,"Play rate");
            mvwprintw(win,2,0,"%f",aeng.m_par_playrate.load());
            mvwprintw(win,1,12,"Pitch");
            mvwprintw(win,2,12,"%f",aeng.m_par_pitch.load());
            mvwprintw(win,1,24,"Pos spread");
            mvwprintw(win,2,24,"%f",aeng.m_par_srcposrand.load());
            mvwprintw(win,1,36,"Overlap");
            mvwprintw(win,2,36,"%f",aeng.m_par_lenmultip.load());
            mvwprintw(win,1,48,"Grain rate");
            mvwprintw(win,2,48,"%f",aeng.m_par_grainrate.load());
            mvwprintw(win,1,60,"Rev prob");
            mvwprintw(win,2,60,"%f",aeng.m_par_reverseprob.load());
            mvwprintw(win,1,72,"Pan spread");
            mvwprintw(win,2,72,"%f",aeng.m_par_stereo_spread.load());
            mvwprintw(win,1,84,"Out/In mix");
            mvwprintw(win,2,84,"%f",aeng.m_par_inputmix.load());
        }
        if (aeng.m_page_state == 1)
        {
            mvwprintw(win,1,12,"Pitch spread");
            mvwprintw(win,2,12,"%f",aeng.m_par_pitchsrpead.load());
            mvwprintw(win,1,84,"WS Morph");
            mvwprintw(win,2,84,"%f",aeng.m_par_waveshapemorph.load());
        }
        if (aeng.m_page_state > 1)
        {
            int paroffs = 0;
            if (aeng.m_page_state == 3)
                paroffs = 8;
            for (int i=0;i<8;++i)
            {
                auto parname = aeng.m_clap_host->getParameterName(paroffs+i);
                auto parform = aeng.m_clap_host->getParameterValueFormatted(paroffs+i);
                int xc = i % 4;
                int yc = i / 4;
                mvwprintw(win,1+2*yc, 24*xc,parname.c_str());
                mvwprintw(win,2+2*yc, 24*xc,parform.c_str());
            }
        }
        float rpos = drsrc->getRecordPosition();
        if (rpos >= 0.0f)
        {
            if (drsrc->m_recordState == 1)
                mvwprintw(win,8,0,"Recording input at %.1f seconds",rpos*5*60.0);
            if (drsrc->m_recordState == 2)
                mvwprintw(win,8,0,"Overdubbing input at %.1f seconds",rpos*5*60.0);
        } 
        std::stringstream ss;
        ss << std::setprecision(2);
        for (auto& e : aeng.m_eng->m_markers)
            ss << e << " ";
        std::string str = ss.str();
        mvwprintw(win,5,0,"Markers : %s",str.c_str());
        mvwprintw(win,6,0,"%.1f seconds of output recorded",aeng.m_recseconds.load());
        auto regrng = aeng.m_eng->getActiveRegionRange();
        regrng.first *= 5.0f*60.0f;
        regrng.second *= 5.0f*60.0f;
        float tpos = aeng.m_eng->m_gm->getSourcePlayPosition()/44100.0f;
        mvwprintw(win,7,0,"Region %.2f - %.2f Playpos %.2f Active Reel %d",
            regrng.first,regrng.second,tpos,aeng.m_active_reel.load());
        wrefresh(win);
        refresh();
    }
    delwin(win);
    endwin();
    quit_thread = true;
    worker_th.join();
    saveSettings(aeng);
    //aeng.saveOutputBuffer("recorded_output.wav");
    return 0;
}
#endif
