#include "plugin.hpp"
#include "wdl/resample.h"
#include "helperwidgets.h"

class SamplerVoice
{
public:
    SamplerVoice()
    {
        const char* fn = "C:\\MusicAudio\\sourcesamples\\sheila.wav";
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
    float process(float deltatime, float outsamplerate, float pitch)
    {
        if (mUpdateCounter == mUpdateLen)
        {
            mUpdateCounter = 0;
            double ratio = std::pow(2.0,1.0/12*pitch);
            float result[2] = {0.0,0.0};
            m_src.SetRates(m_srcsampleRate,m_srcsampleRate/ratio);
            float* rsinbuf = nullptr;
            int wanted = m_src.ResamplePrepare(mUpdateLen,1,&rsinbuf);
            for (int i=0;i<wanted;++i)
            {
                rsinbuf[i] = pSampleData[m_phase];
                m_phase += 1;
                if (m_phase>=m_totalPCMFrameCount)
                    m_phase = 0;
                
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
};

class XSampler : public Module
{
public:
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
        config(PAR_LAST,0,OUT_LAST);
        configParam(PAR_PITCH,-60.0f,60.0f,0.0f);
    }
    void process(const ProcessArgs& args) override
    {
        float pitch = params[PAR_PITCH].getValue();
        float sum = 0.0f;
        for (int i=0;i<16;++i)
        {
            float vpitch = pitch+i*0.1;
            float s = m_voices[i].process(args.sampleTime,args.sampleRate,vpitch);
            sum += s;
        }
        sum *= 0.3;
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
