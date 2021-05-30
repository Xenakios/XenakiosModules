#include "plugin.hpp"
#include <random>
#include "jcdp_envelope.h"
#include "helperwidgets.h"

extern std::shared_ptr<Font> g_font;

inline double quantize(double x, double step, double amount)
{
    double quan = std::round(x/step)*step;
    double diff = quan - x;
    return x+diff*amount;
}

/*
voice minpitch maxpitch pitch maxdur gate  par1  par2  par3  par4
1     24.0     48.0     [out] 3.0    [out] [out] [out] [out] [out]


ppp pp p mp mf f ff 
  1  2 3  4  5 6  7

*/

const int EnvelopeTable[6][4] =
{
    {1,1,0,0}, // ppp
    {2,1,2,0}, // ppp < p
    {3,1,2,1}, // ppp < p > ppp
    {2,2,1,0}, // p > ppp
    {2,1,3,0}, // p < f
    {3,1,3,1}, // ppp < f > ppp
};

/*
const float EnvelopeScalers[2][4] =
{
    {1.0f/7*1,1.0f/7*3,1.0f/7*6,1.0f},
    {0.0,1.0f/3*1,1.0f/3*2,1.0f}
};
*/

inline float Kumaraswamy(float z)
{
    float k_a = 0.3f;
    float k_b = 0.5f;
    float k_x = 1.0f-powf((1.0f-z),1.0f/k_b);
    return powf(k_x,1.0f/k_a);
}

class StocVoice
{
public:
    StocVoice()
    {
        m_pitch_env.AddNode({0.0f,0.0f,2});
        m_pitch_env.AddNode({1.0f,0.0f,2});

        m_par1_env.AddNode({0.0f,0.0f,2});
        m_par1_env.AddNode({1.0f,0.0f,2});

        m_par2_env.AddNode({0.0f,0.0f,2});
        m_par2_env.AddNode({1.0f,0.0f,2});
    }
    void process(float deltatime, float* gate,float* pitch,float* amp,float* par1, float* par2)
    {
        *gate = 10.0f;
        
        float normphase = 1.0f/m_len*m_phase;
        float gain = m_amp_env->GetInterpolatedEnvelopeValue(normphase);
        *amp = rescale(gain,0.0f,1.0f,0.0f,10.0f);
        *pitch = reflect_value<float>(-60.0f,m_pitch + m_pitch_env.GetInterpolatedEnvelopeValue(normphase),60.0f);
        *par1 = reflect_value<float>(-5.0f,m_par1 + m_par1_env.GetInterpolatedEnvelopeValue(normphase),5.0f);
        *par2 = reflect_value<float>(-5.0f,m_par2 + m_par2_env.GetInterpolatedEnvelopeValue(normphase),5.0f);
        m_phase += deltatime;
        if (m_phase>=m_len)
        {
            m_available = true;
        }
    }
    bool isAvailable()
    {
        return m_available;
    }
    void start(float dur, float centerpitch,float spreadpitch, breakpoint_envelope* ampenv,
        float glissprob, float gliss_spread, int penv)
    {
        std::uniform_real_distribution<float> dist(0.0f,1.0f);
        std::uniform_int_distribution<int> shapedist(0,msnumtables-1);
        std::normal_distribution<float> normdist(0.0f,1.0f);
        m_amp_env = ampenv;
        m_phase = 0.0;
        m_len = dur;
        m_pitch = rescale(dist(*m_rng),0.0f,1.0f,centerpitch-spreadpitch,centerpitch+spreadpitch);
        float glissdest = 0.0;
        auto& pt0 = m_pitch_env.GetNodeAtIndex(0);
        auto& pt1 = m_pitch_env.GetNodeAtIndex(1);
        if (dist(*m_rng)<glissprob)
        {
            if (gliss_spread<0.0f)
            {
                float kuma = Kumaraswamy(dist(*m_rng));
                float spr = rescale(gliss_spread,-1.0f,0.0f,36.0f,0.0f);
                glissdest = rescale(kuma,0.0f,1.0f,-spr,spr);
            }
            else if (gliss_spread<0.99)
            {
                glissdest = normdist(*m_rng)*rescale(gliss_spread,0.0f,1.0f,0.0f,24.0f);
            }
            else
            {
                float z = dist(*m_rng);
                float cauchy = std::tan(M_PI*(z-0.5));
                glissdest = clamp(cauchy,-36.0f,36.0f);
            }
            int shap = penv-1;
            if (shap<0)
                pt0.Shape = shapedist(*m_rng);
            else
                pt0.Shape = shap;
        }
        
        pt1.pt_y = glissdest;
        m_par1 = rescale(dist(*m_rng),0.0f,1.0f,-5.0f,5.0f);
        float pardest = rescale(dist(*m_rng),0.0f,1.0f,-5.0f,5.0f);
        m_par1_env.GetNodeAtIndex(1).pt_y = pardest;
        m_par2 = rescale(dist(*m_rng),0.0f,1.0f,-5.0f,5.0f);
        
        float kuma = Kumaraswamy(dist(*m_rng));
        pardest = rescale(kuma,0.0f,1.0f,-5.0f,5.0f);
        m_par2_env.GetNodeAtIndex(1).pt_y = pardest;
        
        m_available = false;
    }
    void reset()
    {
        m_available = true;
        m_phase = 0.0;
    }
    float m_playProb = 1.0f;
    float m_startPos = 0.0f;
    std::mt19937* m_rng = nullptr;
private:
    bool m_available = true;
    breakpoint_envelope m_pitch_env;
    breakpoint_envelope* m_amp_env = nullptr;
    breakpoint_envelope m_par1_env;
    breakpoint_envelope m_par2_env;
    double m_phase = 0.0;
    double m_len = 0.5;
    float m_min_pitch = -24.0f;
    float m_max_pitch = 24.0f;
    float m_pitch = 0.0f;
    float m_glissrange = 0.0f;
    float m_par1 = 0.0f;
    float m_par2 = 0.0f;
};

class XStochastic : public rack::Module
{
public:
    enum INPUTS
    {
        IN_RESET,
        IN_PITCH_CENTER,
        IN_RATE,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_GATE,
        OUT_PITCH,
        OUT_VCA,
        OUT_AUX1, 
        OUT_AUX2,
        OUT_LAST
    };
    enum PARAMS
    {
        PAR_MASTER_MEANDUR,
        PAR_MASTER_GLISSPROB,
        PAR_MASTER_DENSITY,
        PAR_MASTER_RANDSEED,
        PAR_MASTER_GLISS_SPREAD,
        PAR_MASTER_PITCH_CENTER,
        PAR_MASTER_PITCH_SPREAD,
        PAR_NUM_OUTPUTS,
        PAR_MASTER_PITCH_ENV_TYPE,
        PAR_MASTER_AMP_ENV_TYPE,
        PAR_RATE_CV,
        PAR_RATE_QUAN_STEP,
        PAR_RATE_QUAN_AMOUNT,
        PAR_LAST
    };
    int m_numAmpEnvs = 11;
    XStochastic()
    {
        for (int i=0;i<16;++i)
        {
            m_voices[i].m_rng = &m_rng;
        }
        m_amp_envelopes[0].AddNode({0.0,0.0,2});
        m_amp_envelopes[0].AddNode({0.5,1.0,2});
        m_amp_envelopes[0].AddNode({1.0,0.0});
        
        m_amp_envelopes[1].AddNode({0.0,0.0,4});
        m_amp_envelopes[1].AddNode({0.01,1.0,4});
        m_amp_envelopes[1].AddNode({1.0,0.0});

        m_amp_envelopes[2].AddNode({0.0,0.0,2});
        m_amp_envelopes[2].AddNode({0.9,1.0,2});
        m_amp_envelopes[2].AddNode({1.0,0.0});

        m_amp_envelopes[3].AddNode({0.0,0.0,2});
        m_amp_envelopes[3].AddNode({0.1,0.2,2});
        m_amp_envelopes[3].AddNode({0.9,0.2,2});
        m_amp_envelopes[3].AddNode({1.0,0.0,2});
        
        m_amp_envelopes[4].AddNode({0.00,0.0,2});
        m_amp_envelopes[4].AddNode({0.01,1.0,2});
        m_amp_envelopes[4].AddNode({0.50,0.05,2});
        m_amp_envelopes[4].AddNode({0.99,1.0,2});
        m_amp_envelopes[4].AddNode({1.00,0.0,2});
        
        m_amp_envelopes[5].AddNode({0.00,0.0,2});
        m_amp_envelopes[5].AddNode({0.01,1.0,2});
        m_amp_envelopes[5].AddNode({0.1,0.1,2});
        m_amp_envelopes[5].AddNode({0.90,0.1,2});
        m_amp_envelopes[5].AddNode({1.00,0.0,2});

        m_amp_envelopes[6].AddNode({0.00,0.0,2});
        m_amp_envelopes[6].AddNode({0.10,0.1,2});
        m_amp_envelopes[6].AddNode({0.90,0.1,2});
        m_amp_envelopes[6].AddNode({0.99,1.0,2});
        m_amp_envelopes[6].AddNode({1.00,0.0,2});

        m_amp_envelopes[7].AddNode({0.00,0.0,2});
        m_amp_envelopes[7].AddNode({0.50,0.5,2});
        m_amp_envelopes[7].AddNode({0.90,0.5,2});
        m_amp_envelopes[7].AddNode({1.00,0.0,2});
        auto envgen = [](int numiters, breakpoint_envelope& env)
        {
            float itlen = 1.0/numiters;
            for (int i=0;i<numiters;++i)
            {
                float t = 1.0/numiters*i;
                float g = 1.0-0.95/numiters*i;
                env.AddNode({t+0.00,0.0,2});
                env.AddNode({t+itlen*0.01,1.0*g,2});
                env.AddNode({t+itlen*0.20,0.2*g,2});
                env.AddNode({t+itlen*0.99,0.0,2});
            }
        };
        
        envgen(5,m_amp_envelopes[8]);
        envgen(6,m_amp_envelopes[9]);
        envgen(7,m_amp_envelopes[10]);

        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_MASTER_MEANDUR,0.1,2.0,0.5,"Master mean duration");
        configParam(PAR_MASTER_GLISSPROB,0.0,1.0,0.5,"Master glissando probability");
        //configParam(PAR_MASTER_DENSITY,0.0,1.0,0.25,"Master density");
        configParam(PAR_MASTER_DENSITY, -3.f, 5.f, 1.f, "Master density", " events per second", 2, 1);
        configParam(PAR_MASTER_RANDSEED,0.0,512.0,256.0,"Master random seed");
        configParam(PAR_MASTER_GLISS_SPREAD,-1.0,1.0,0.2,"Master glissando spread");
        configParam(PAR_MASTER_PITCH_CENTER,-48.0,48.0,0.0,"Master pitch center");
        configParam(PAR_MASTER_PITCH_SPREAD,0,48.0,12.0,"Master pitch spread");
        configParam(PAR_NUM_OUTPUTS,1,16.0,8.0,"Number of outputs");
        configParam(PAR_MASTER_PITCH_ENV_TYPE,0,msnumtables,0.0,"Pitch envelope type");
        configParam(PAR_MASTER_AMP_ENV_TYPE,0,m_numAmpEnvs,0.0,"VCA envelope type");
        configParam(PAR_RATE_CV,-1.0f,1.0f,0.0,"Master density CV ATTN");
        configParam(PAR_RATE_QUAN_STEP,0.001f,1.0f,0.001,"Rate quantization step");
        configParam(PAR_RATE_QUAN_AMOUNT,0.0f,1.0f,0.0,"Rate quantization amount");
        m_rng = std::mt19937(256);
    }
    int m_curRandSeed = 256;
    int m_NumUsedVoices = 0;
    int m_eventCounter = 0;
    void process(const ProcessArgs& args) override
    {
        int rseed = params[PAR_MASTER_RANDSEED].getValue();
        if (rseed!=m_curRandSeed)
        {
            m_curRandSeed = rseed;
            
        }
        int numvoices = params[PAR_NUM_OUTPUTS].getValue();
        if (m_phase >= m_nextEventPos)
        {
            //++m_eventCounter;
            std::uniform_real_distribution<float> dist(0.0f,1.0f);
            std::uniform_int_distribution<int> voicedist(0,numvoices-1);
            std::uniform_int_distribution<int> vcadist(0,m_numAmpEnvs-1);
            std::normal_distribution<float> durdist(0.0f,1.0f);
            float glissprob = params[PAR_MASTER_GLISSPROB].getValue();
            float gliss_spread = params[PAR_MASTER_GLISS_SPREAD].getValue();
            float meandur = params[PAR_MASTER_MEANDUR].getValue();
            float durdev = rescale(meandur,0.1,2.0,0.1,1.0);
            float density = params[PAR_MASTER_DENSITY].getValue();
            density += inputs[IN_RATE].getVoltage()*params[PAR_RATE_CV].getValue();
            density = clamp(density,-3.0f,5.0f);
            density = std::pow(2.0f,density);
            float centerpitch = params[PAR_MASTER_PITCH_CENTER].getValue();
            int numpitchcvchans = inputs[IN_PITCH_CENTER].getChannels();
            if (numpitchcvchans>0)
            {
                std::uniform_int_distribution<int> pitchdist(0,inputs[IN_PITCH_CENTER].getChannels()-1);
                int indx = pitchdist(m_rng);
                centerpitch += inputs[IN_PITCH_CENTER].getVoltage(indx)*12.0;
                centerpitch = clamp(centerpitch,-60.0,60.0f);
            }
            float spreadpitch = params[PAR_MASTER_PITCH_SPREAD].getValue();
            int manual_amp_env = params[PAR_MASTER_AMP_ENV_TYPE].getValue();
            int manual_pitch_env = params[PAR_MASTER_PITCH_ENV_TYPE].getValue();
            int i = 0;
            while (i<numvoices)
            {
                int voiceIndex = voicedist(m_rng);
                if (m_voices[voiceIndex].isAvailable())
                {
                    m_voices[voiceIndex].m_startPos = m_nextEventPos;
                    float evdur = meandur + durdist(m_rng)*durdev;
                    evdur = clamp(evdur,0.05,8.0);
                    int ampenv = manual_amp_env - 1;
                    if (ampenv < 0)
                        ampenv = vcadist(m_rng);
                    m_voices[voiceIndex].start(evdur,centerpitch,spreadpitch,
                        &m_amp_envelopes[ampenv],glissprob,gliss_spread,manual_pitch_env);
                    ++m_eventCounter;
                    break;
                }
                ++i;
            }
            double qamt = params[PAR_RATE_QUAN_AMOUNT].getValue();
            double deltatime = -log(dist(m_rng))/density;
            deltatime = clamp(deltatime,args.sampleTime,30.0f);
            double evpos = m_nextEventPos + deltatime;
            evpos = quantize(evpos,params[PAR_RATE_QUAN_STEP].getValue(),qamt);
            if ((evpos-m_nextEventPos)<0.002)
            {
                evpos = m_phase + 0.002;
                //++m_eventCounter;
            }
            //++m_eventCounter;
            m_nextEventPos = evpos;
            //m_nextEventPos += deltatime;
        }
        m_NumUsedVoices = 0;
        outputs[OUT_PITCH].setChannels(numvoices);
        outputs[OUT_GATE].setChannels(numvoices);
        outputs[OUT_VCA].setChannels(numvoices);
        outputs[OUT_AUX1].setChannels(numvoices);
        outputs[OUT_AUX2].setChannels(numvoices);
        for (int i=0;i<numvoices;++i)
        {
            float gate = 0.0f;
            float amp = 0.0f;
            float pitch = 0.0f;
            float par1 = 0.0f;
            float par2 = 0.0f;
            if (m_voices[i].isAvailable()==false && m_phase>=m_voices[i].m_startPos)
            {
                m_voices[i].process(args.sampleTime,&gate,&pitch,&amp,&par1,&par2);
                ++m_NumUsedVoices;
            }
            outputs[OUT_GATE].setVoltage(gate,i);
            pitch = pitch*(1.0f/12);
            outputs[OUT_PITCH].setVoltage(pitch,i);
            outputs[OUT_VCA].setVoltage(amp,i);
            outputs[OUT_AUX1].setVoltage(par1,i);
            outputs[OUT_AUX2].setVoltage(par2,i);
        }
        if (m_resetTrigger.process(rescale(inputs[IN_RESET].getVoltage(),0.0,10.0, 0.f, 1.f)))
        {
            m_nextEventPos = 0.0;
            m_phase = 0.0;
            m_eventCounter = 0;
            m_rng = std::mt19937(m_curRandSeed);
            for (int i=0;i<numvoices;++i)
            {
                m_voices[i].reset();
            }
        }
        m_phase += args.sampleTime;
    }
    unsigned int m_randSeed = 1;
private:
    double m_phase = 0.0f;
    double m_nextEventPos = 0.0f;
    std::mt19937 m_rng{m_randSeed};
    StocVoice m_voices[16];
    breakpoint_envelope m_amp_envelopes[16];
    
    dsp::SchmittTrigger m_resetTrigger;
};

class XStochasticWidget : public ModuleWidget
{
public:
    
    XStochasticWidget(XStochastic* m)
    {
        setModule(m);
        box.size.x = RACK_GRID_WIDTH*22;
        
        if (!g_font)
        	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
        
        addOutput(createOutput<PJ301MPort>(Vec(5, 20), module, XStochastic::OUT_GATE));
        addOutput(createOutput<PJ301MPort>(Vec(30, 20), module, XStochastic::OUT_PITCH));
        addOutput(createOutput<PJ301MPort>(Vec(55, 20), module, XStochastic::OUT_VCA));
        addOutput(createOutput<PJ301MPort>(Vec(80, 20), module, XStochastic::OUT_AUX1));
        addOutput(createOutput<PJ301MPort>(Vec(105, 20), module, XStochastic::OUT_AUX2));
        
        addInput(createInput<PJ301MPort>(Vec(5, 330), module, XStochastic::IN_RESET));
        float lfs = 9.0f;
        float xc = 2.0f;
        float yc = 50.0f;
        addChild(new KnobInAttnWidget(this,"RATE",XStochastic::PAR_MASTER_DENSITY,
            XStochastic::IN_RATE,XStochastic::PAR_RATE_CV,xc,yc,false,lfs));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"MEAN DURATION",XStochastic::PAR_MASTER_MEANDUR,
            -1,-1,xc,yc));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"MEAN PITCH",XStochastic::PAR_MASTER_PITCH_CENTER,
            XStochastic::IN_PITCH_CENTER,-1,xc,yc));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"PITCH SPREAD",XStochastic::PAR_MASTER_PITCH_SPREAD,
            -1,-1,xc,yc));
        xc = 2;
        yc += 47;
        addChild(new KnobInAttnWidget(this,"GLISS PROBABILITY",XStochastic::PAR_MASTER_GLISSPROB,
            -1,-1,xc,yc));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"GLISS SPREAD",XStochastic::PAR_MASTER_GLISS_SPREAD,
            -1,-1,xc,yc));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"GLISS ENV TYPE",XStochastic::PAR_MASTER_PITCH_ENV_TYPE,
            -1,-1,xc,yc,true));
        xc += 82;            
        addChild(new KnobInAttnWidget(this,"RANDOM SEED",XStochastic::PAR_MASTER_RANDSEED,
            -1,-1,xc,yc,true));
        xc = 2;
        yc += 47;
        addChild(new KnobInAttnWidget(this,"VCA ENV TYPE",XStochastic::PAR_MASTER_AMP_ENV_TYPE,
            -1,-1,xc,yc,true));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"NUM POLY OUTS",XStochastic::PAR_NUM_OUTPUTS,
            -1,-1,xc,yc,true));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"TIME QUANT STEP",XStochastic::PAR_RATE_QUAN_STEP,
            -1,-1,xc,yc));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"TIME QUANT AMOUNT",XStochastic::PAR_RATE_QUAN_AMOUNT,
            -1,-1,xc,yc,false,lfs));
    }
    ~XStochasticWidget()
    {
        
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
        nvgFontFaceId(args.vg, g_font->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        nvgText(args.vg, 3 , 10, "ST(ochastic)", NULL);
        char buf[100];
        XStochastic* sm = dynamic_cast<XStochastic*>(module);
        if (sm)
            sprintf(buf,"Xenakios %d voices, %d events",sm->m_NumUsedVoices,sm->m_eventCounter);
        else sprintf(buf,"Xenakios");
        nvgText(args.vg, 3 , h-11, buf, NULL);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
private:
    
};

Model* modelXStochastic = createModel<XStochastic, XStochasticWidget>("XStochastic");
