#include "plugin.hpp"
#include "../wdl/resample.h"
#include "helperwidgets.h"
#include <osdialog.h>
#include "dr_wav.h"

class SampleData
{
public:
    SampleData() {}
    ~SampleData() 
    {
        drwav_free(pSampleData,nullptr);
    }
    void loadFile(std::string fn)
    {
        unsigned int chans = 0;
        unsigned int sr = 0;
        drwav_uint64 len = 0;
        float* data = drwav_open_file_and_read_pcm_frames_f32(
            fn.c_str(),
            &chans,
            &sr,
            &len,
            nullptr);
        if (data)
        {
            m_lock.lock();
            std::swap(pSampleData,data);
            m_channels = chans;
            m_srcSamplerate = sr;
            m_frameCount = len;
            m_lock.unlock();
            drwav_free(data,nullptr);
        }
    }

//private:
    float* pSampleData = nullptr;
    unsigned int m_channels = 0;
    unsigned int m_srcSamplerate = 0;
    drwav_uint64 m_frameCount;
    spinlock m_lock;
};

class SamplerVoice
{
public:
    std::shared_ptr<SampleData> m_sampleData;
    SamplerVoice(std::shared_ptr<SampleData> sd) : m_sampleData(sd)
    {
        
        srcInBuffer.resize(64);
        srcOutBuffer.resize(64);
        //m_src.SetMode(false,0,false);
    }
    ~SamplerVoice()
    {
        
    }
    std::atomic<float*> mNewSampleData{nullptr};
    void process(float* outbuf, int outchans, float deltatime, float outsamplerate, float pitch, float trig, float lin_rate)
    {
        if (m_sampleData == nullptr)
            return;
        m_sampleData->m_lock.lock();
        if (m_phase>=m_sampleData->m_frameCount)
            m_phase = 0;
        if (m_trig.process(rescale(trig,0.0f,10.0f,0.0f,1.0f)))
        {
            m_phase = 0;
        }
        
        int srcChannels = m_sampleData->m_channels;
        if (mUpdateCounter == mUpdateLen)
        {
            mUpdateCounter = 0;
            double ratio = std::pow(2.0,1.0/12*pitch);
            int samp_inc = 1;
            if (lin_rate<0.0f)
                samp_inc = -1;
            float arate = std::abs(lin_rate);
            if (arate<0.001)
                arate = 0.001;
            float result[2] = {0.0,0.0};
            m_src.SetRates(m_sampleData->m_srcSamplerate,outsamplerate/(ratio*arate));
            float* rsinbuf = nullptr;
            
            float* samplePtr = m_sampleData->pSampleData;
            auto numFrames = m_sampleData->m_frameCount;
            int wanted = m_src.ResamplePrepare(mUpdateLen, srcChannels,&rsinbuf);
            for (int i=0;i<wanted;++i)
            {
                for (int j=0;j<srcChannels;++j)
                    rsinbuf[i*srcChannels+j] = samplePtr[m_phase*srcChannels+j];
                m_phase += samp_inc;
                if (m_phase>=(int)numFrames)
                    m_phase = 0;
                if (m_phase<0)
                    m_phase = numFrames-1;    
            }
            m_src.ResampleOut(srcOutBuffer.data(),wanted,mUpdateLen,srcChannels);
        }
        if (outchans == srcChannels)
        {
            for (int i=0;i<srcChannels;++i)
                outbuf[i] = srcOutBuffer[mUpdateCounter*srcChannels+i];
        }
        if (outchans == 2 && srcChannels == 1)
        {
            for (int i=0;i<outchans;++i)
                outbuf[i] = srcOutBuffer[mUpdateCounter];
        }
        ++mUpdateCounter;
        m_sampleData->m_lock.unlock();
    }
private:
    WDL_Resampler m_src;
    int m_phase = 0;
    std::vector<float> srcInBuffer;
    std::vector<float> srcOutBuffer;
    int mUpdateCounter = 0;
    int mUpdateLen = 8;
    dsp::SchmittTrigger m_trig;
};

class XSampler : public Module
{
public:
    enum INS
    {
        IN_PITCH,
        IN_TRIG,
        IN_LIN_RATE,
        IN_LAST
    };
    enum OUTS
    {
        OUT_AUDIO,
        OUT_LAST
    };
    enum PARAMS
    {
        PAR_PITCH,
        PAR_OUTPUTCHANSMODE,
        PAR_LAST
    };
    std::shared_ptr<SampleData> m_zone0;
    XSampler()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_PITCH,-60.0f,60.0f,0.0f);
        configParam(PAR_OUTPUTCHANSMODE,0.0f,1.0f,1.0f);
        std::string fn = asset::plugin(pluginInstance, "res/samples/kampitam1.wav");
        m_zone0 = std::make_shared<SampleData>();
        m_zone0->loadFile(fn);
        for (int i=0;i<16;++i)
        {
            m_voices.emplace_back(std::make_shared<SamplerVoice>(m_zone0));
        }
    }
    void process(const ProcessArgs& args) override
    {
        float pitch = params[PAR_PITCH].getValue();
        
        int numvoices = inputs[IN_PITCH].getChannels();
        if (numvoices==0)
            numvoices = 1;
        float linrate = 1.0f; 
        if (inputs[IN_LIN_RATE].isConnected())
        {
            linrate = inputs[IN_LIN_RATE].getVoltage()*0.2f;
            linrate = clamp(linrate,-1.0f,1.0f);
        }
        float sum[2] = {0.0f,0.0f};
        int omode = params[PAR_OUTPUTCHANSMODE].getValue();
        int voicechans = 2;
        for (int i=0;i<numvoices;++i)
        {
            float vpitch = pitch+inputs[IN_PITCH].getVoltage(i)*12.0f;
            vpitch = clamp(vpitch,-60.0f,60.0f);
            float trig = 0.0f;
            if (inputs[IN_TRIG].getChannels()>1)
                trig = inputs[IN_TRIG].getVoltage(i);
            else trig = inputs[IN_TRIG].getVoltage(0);
            float abuf[2] = {0.0f,0.0f};
            m_voices[i]->process(abuf,voicechans,args.sampleTime,args.sampleRate,vpitch,trig,linrate);
            sum[0] += abuf[0];
            sum[1] += abuf[1];
        }
        
        if (omode == 0)
        {
            outputs[OUT_AUDIO].setChannels(1);
            outputs[OUT_AUDIO].setVoltage((sum[0]+sum[1])*5.0f,0);
        }
        else
        {
            outputs[OUT_AUDIO].setChannels(2);
            outputs[OUT_AUDIO].setVoltage(sum[0]*5.0f,0);
            outputs[OUT_AUDIO].setVoltage(sum[1]*5.0f,1);
        }
    }
private:
    std::vector<std::shared_ptr<SamplerVoice>> m_voices;
};

class XSamplerWidget : public ModuleWidget
{
public:
    XSamplerWidget(XSampler* m)
    {
        setModule(m);
        box.size.x = RACK_GRID_WIDTH * 30;
        PortWithBackGround* port = nullptr;
        port = new PortWithBackGround(m,this,XSampler::OUT_AUDIO,1, 20,"AUDIO OUT",true);
        port = new PortWithBackGround(m,this,XSampler::IN_PITCH,52, 20,"1V/OCT",false);
        port = new PortWithBackGround(m,this,XSampler::IN_TRIG,82, 20,"GT/TR",false);
        port = new PortWithBackGround(m,this,XSampler::IN_LIN_RATE,112, 20,"LINRATE",false);
        addParam(createParam<Trimpot>(Vec(30, 20), m, XSampler::PAR_PITCH)); 
    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x50, 0x50, 0x50, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);

        nvgFontSize(args.vg, 15);
        nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        nvgText(args.vg, 3 , 10, "SAMPLER", NULL);
        char buf[100];
        sprintf(buf,"Xenakios");
        nvgText(args.vg, 3 , h-9, buf, NULL);
        
        
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
    struct LoadFileItem : MenuItem
    {
    XSampler* m_mod = nullptr;
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
        m_mod->m_zone0->loadFile(path);
    }
};
    void appendContextMenu(Menu *menu) override 
    {
		auto loadItem = createMenuItem<LoadFileItem>("Import .wav file...");
		loadItem->m_mod = dynamic_cast<XSampler*>(module);
		menu->addChild(loadItem);
        /*
        auto drsrc = dynamic_cast<DrWavSource*>(m_gm->m_eng.m_srcs[0].get());
        auto normItem = createMenuItem([this,drsrc](){ drsrc->normalize(1.0f); },"Normalize buffer");
        menu->addChild(normItem);
        auto revItem = createMenuItem([this,drsrc](){ drsrc->reverse(); },"Reverse buffer");
        menu->addChild(revItem);
        */
    }
};

Model* modelXSampler = createModel<XSampler, XSamplerWidget>("XSampler");
