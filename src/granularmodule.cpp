#include "plugin.hpp"
#include "grain_engine/grain_engine.h"
#define DR_WAV_IMPLEMENTATION
#include "grain_engine/dr_wav.h"
#include "helperwidgets.h"

class DrWavSource : public GrainAudioSource
{
public:
    float* m_pSampleData = nullptr;
    unsigned int m_channels = 0;
    unsigned int m_sampleRate = 0;
    drwav_uint64 m_totalPCMFrameCount = 0;
    DrWavSource()
    {
        
#ifdef __APPLE__
        std::string filename("/Users/teemu/AudioProjects/sourcesamples/db_guit01.wav");
#else
        std::string filename("C:\\MusicAudio\\sourcesamples\\windchimes_c1.wav");
#endif
        m_pSampleData = drwav_open_file_and_read_pcm_frames_f32(
            filename.c_str(), 
            &m_channels, 
            &m_sampleRate, 
            &m_totalPCMFrameCount, 
            NULL);

        if (m_pSampleData == NULL) {
            std::cout << "could not open wav with dr wav\n";
            return;
        }
    }
    void putIntoBuffer(float* dest, int frames, int channels, int startInSource) override
    {
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
                    dest[i*channels+j] = m_pSampleData[index*m_channels+actsrcchan];
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
        drwav_free(m_pSampleData,nullptr);
    }
    int getSourceNumChannels() override
    {
        return m_channels;
    }
};

class GrainEngine
{
public:
    GrainEngine()
    {

    }
    void process(float sr,float* buf, float playrate, float pitch, float loopstart, float looplen, float posrand)
    {
        buf[0] = 0.0f;
        buf[1] = 0.0f;
        buf[2] = 0.0f;
        buf[3] = 0.0f;
        m_gm.m_sr = sr;
        m_gm.m_grainDensity = 0.05f;
        m_gm.m_inputdur = m_src.m_totalPCMFrameCount;
        m_gm.m_loopstart = loopstart;
        m_gm.m_looplen = looplen;
        m_gm.m_sourcePlaySpeed = playrate;
        m_gm.m_pitch = pitch;
        m_gm.m_posrandamt = posrand;
        m_gm.processAudio(buf);
    }
    DrWavSource m_src;
    GrainMixer m_gm{&m_src};
private:
    
};

class XGranularModule : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_PLAYRATE,
        PAR_PITCH,
        PAR_LOOPSTART,
        PAR_LOOPLEN,
        PAR_ATTN_PLAYRATE,
        PAR_ATTN_PITCH,
        PAR_SRCPOSRANDOM,
        PAR_LAST
    };
    enum OUTPUTS
    {
        OUT_AUDIO,
        OUT_LAST
    };
    enum INPUTS
    {
        IN_CV_PLAYRATE,
        IN_CV_PITCH,
        IN_LAST
    };
    XGranularModule()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_PLAYRATE,-2.0f,2.0f,1.0f,"Playrate");
        configParam(PAR_PITCH,-24.0f,24.0f,0.0f,"Pitch");
        configParam(PAR_LOOPSTART,0.0f,1.0f,0.0f,"Loop start");
        configParam(PAR_LOOPLEN,0.0f,1.0f,1.0f,"Loop length");
        configParam(PAR_ATTN_PLAYRATE,-1.0f,1.0f,0.0f,"Playrate CV ATTN");
        configParam(PAR_ATTN_PITCH,-1.0f,1.0f,0.0f,"Pitch CV ATTN");
        configParam(PAR_SRCPOSRANDOM,0.0f,1.0f,0.0f,"Source position randomization");
    }
    void process(const ProcessArgs& args) override
    {
        float buf[4] ={0.0f,0.0f,0.0f,0.0f};
        float prate = params[PAR_PLAYRATE].getValue();
        prate += inputs[IN_CV_PLAYRATE].getVoltage()*params[PAR_ATTN_PLAYRATE].getValue()/10.0f;
        prate = clamp(prate,-2.0f,2.0f);
        float pitch = params[PAR_PITCH].getValue();
        pitch += inputs[IN_CV_PITCH].getVoltage()*12.0f;
        pitch = clamp(pitch,-24.0f,24.0f);
        float loopstart = params[PAR_LOOPSTART].getValue();
        float looplen = params[PAR_LOOPLEN].getValue();
        float posrnd = params[PAR_SRCPOSRANDOM].getValue();
        m_eng.process(args.sampleRate, buf,prate,pitch,loopstart,looplen,posrnd);
        outputs[OUT_AUDIO].setVoltage(buf[0]*5.0f);
        graindebugcounter = m_eng.m_gm.debugCounter;
    }
    int graindebugcounter = 0;
private:
    GrainEngine m_eng;
};

class XGranularWidget : public rack::ModuleWidget
{
public:
    XGranularModule* m_gm = nullptr;
    XGranularWidget(XGranularModule* m)
    {
        setModule(m);
        m_gm = m;
        box.size.x = 200;
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "GRAINS",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        PortWithBackGround<PJ301MPort>* port = nullptr;
        addOutput(port = createOutput<PortWithBackGround<PJ301MPort>>(Vec(1, 34), m, XGranularModule::OUT_AUDIO));
        port->m_text = "AUDIO OUT";
        addChild(new KnobInAttnWidget(this,
            "PLAYRATE",XGranularModule::PAR_PLAYRATE,
            XGranularModule::IN_CV_PLAYRATE,XGranularModule::PAR_ATTN_PLAYRATE,1,60));
        addChild(new KnobInAttnWidget(this,
            "PITCH",XGranularModule::PAR_PITCH,XGranularModule::IN_CV_PITCH,-1,82,60));
        addChild(new KnobInAttnWidget(this,"LOOP START",XGranularModule::PAR_LOOPSTART,-1,-1,1,101));
        addChild(new KnobInAttnWidget(this,"LOOP LENGTH",XGranularModule::PAR_LOOPLEN,-1,-1,82,101));
        addChild(new KnobInAttnWidget(this,"SOURCE POS RAND",XGranularModule::PAR_SRCPOSRANDOM,-1,-1,1,142));
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
            sprintf(buf,"%d",m_gm->graindebugcounter);
            nvgFontSize(args.vg, 15);
            nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            
            nvgText(args.vg, 1 , 280, buf, NULL);
        }
        


        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelXGranular = createModel<XGranularModule, XGranularWidget>("XGranular");
