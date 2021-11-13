#include "../plugin.hpp"
#include "../wdl/resample.h"
#include "../helperwidgets.h"

class SamplerVoice
{
public:
    SamplerVoice()
    {
        
    #ifdef __APPLE__
        const char* fn = "/Users/teemu/AudioProjects/sourcesamples/sheila.wav";
    #else
        const char* fn = "C:\\MusicAudio\\sourcesamples\\sheila.wav";
    #endif
        pSampleData = drwav_open_file_and_read_pcm_frames_f32(
            fn,
            &m_channels,
            &m_srcsampleRate,
            &m_totalPCMFrameCount,
            nullptr);
        srcInBuffer.resize(64);
        srcOutBuffer.resize(64);
        //m_src.SetMode(false,0,false);
    }
    ~SamplerVoice()
    {
        drwav_free(pSampleData,nullptr);
    }
    float process(float deltatime, float outsamplerate, float pitch, float trig, float lin_rate)
    {
        if (m_trig.process(rescale(trig,0.0f,10.0f,0.0f,1.0f)))
        {
            m_phase = 0;
        }
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
            m_src.SetRates(m_srcsampleRate,m_srcsampleRate/(ratio*arate));
            float* rsinbuf = nullptr;
            int wanted = m_src.ResamplePrepare(mUpdateLen,1,&rsinbuf);
            for (int i=0;i<wanted;++i)
            {
                rsinbuf[i] = pSampleData[m_phase];
                m_phase += samp_inc;
                if (m_phase>=(int)m_totalPCMFrameCount)
                    m_phase = 0;
                if (m_phase<0)
                    m_phase = m_totalPCMFrameCount-1;    
            }
            m_src.ResampleOut(srcOutBuffer.data(),wanted,mUpdateLen,1);
        }
        float os = srcOutBuffer[mUpdateCounter];
        ++mUpdateCounter;
        return os;
    }
private:
    WDL_Resampler m_src;
    float* pSampleData = nullptr;
    unsigned int m_channels = 0;
    unsigned int m_srcsampleRate = 0;
    drwav_uint64 m_totalPCMFrameCount = 0;
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
        PAR_LAST
    };
    XSampler()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_PITCH,-60.0f,60.0f,0.0f);
    }
    void process(const ProcessArgs& args) override
    {
        float pitch = params[PAR_PITCH].getValue();
        float sum = 0.0f;
        int numvoices = inputs[IN_PITCH].getChannels();
        if (numvoices==0)
            numvoices = 1;
        float linrate = 1.0f; 
        if (inputs[IN_LIN_RATE].isConnected())
        {
            linrate = inputs[IN_LIN_RATE].getVoltage()*0.2f;
            linrate = clamp(linrate,-1.0f,1.0f);
        }
        
        for (int i=0;i<numvoices;++i)
        {
            float vpitch = pitch+inputs[IN_PITCH].getVoltage(i)*12.0f;
            vpitch = clamp(vpitch,-60.0f,60.0f);
            float trig = inputs[IN_TRIG].getVoltage(i);
            float s = m_voices[i].process(args.sampleTime,args.sampleRate,vpitch,trig,linrate);
            sum += s;
        }
        sum *= 0.5;
        outputs[OUT_AUDIO].setVoltage(sum*5.0f);
    }
private:
    SamplerVoice m_voices[16];
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
};

Model* modelXSampler = createModel<XSampler, XSamplerWidget>("XSampler");
