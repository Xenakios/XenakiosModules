#include "plugin.hpp"
#include <random>
#include "jcdp_envelope.h"

extern std::shared_ptr<Font> g_font;

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
        
        m_phase += deltatime;
        float normphase = 1.0f/m_len*m_phase;
        float gain = m_amp_env->GetInterpolatedEnvelopeValue(normphase);
        *amp = rescale(gain,0.0f,1.0f,0.0f,10.0f);
        *pitch = m_pitch + m_pitch_env.GetInterpolatedEnvelopeValue(normphase);
        *par1 = clamp(m_par1 + m_par1_env.GetInterpolatedEnvelopeValue(normphase),-5.0f,5.0f);
        *par2 = clamp(m_par2 + m_par2_env.GetInterpolatedEnvelopeValue(normphase),-5.0f,5.0f);
        if (m_phase>=m_len)
        {
            m_available = true;
        }
    }
    bool isAvailable()
    {
        return m_available;
    }
    void start(float dur, float minpitch,float maxpitch, breakpoint_envelope* ampenv)
    {
        m_amp_env = ampenv;
        m_phase = 0.0;
        m_len = dur;
        m_pitch = rescale(rack::random::uniform(),0.0f,1.0f,minpitch,maxpitch);
        float glissdest = 0.0;
        auto& pt0 = m_pitch_env.GetNodeAtIndex(0);
        auto& pt1 = m_pitch_env.GetNodeAtIndex(1);
        if (random::uniform()<0.5)
        {
            glissdest = rescale(rack::random::uniform(),0.0f,1.0f,-24.0f,24.0f);
            pt0.Shape = random::u32() % 12;
        }
        
        pt1.pt_y = glissdest;
        m_par1 = rescale(rack::random::uniform(),0.0f,1.0f,-5.0f,5.0f);
        float pardest = rescale(rack::random::uniform(),0.0f,1.0f,-5.0f,5.0f);
        m_par1_env.GetNodeAtIndex(1).pt_y = pardest;
        m_par2 = rescale(rack::random::uniform(),0.0f,1.0f,-5.0f,5.0f);
        pardest = rescale(rack::random::uniform(),0.0f,1.0f,-5.0f,5.0f);
        m_par2_env.GetNodeAtIndex(1).pt_y = pardest;
        m_available = false;
    }
    float m_playProb = 1.0f;
    float m_startPos = 0.0f;
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
        IN_LAST
    };
    enum OUTPUTS
    {
        ENUMS(OUT_GATE, 16),
        ENUMS(OUT_PITCH, 16),
        ENUMS(OUT_VCA, 16),
        ENUMS(OUT_AUX1, 16),
        ENUMS(OUT_AUX2, 16)
    };
    int m_numAmpEnvs = 5;
    XStochastic()
    {
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
        
        config(0,IN_LAST,OUT_AUX2_LAST);
    }
    void process(const ProcessArgs& args) override
    {
        if (m_phase>=m_nextEventPos)
        {
            int i = 0;
            while (i<m_maxVoices)
            {
                int voiceIndex = random::u32() % m_maxVoices;
                if (m_voices[voiceIndex].isAvailable())
                {
                    m_voices[voiceIndex].m_startPos = m_nextEventPos;
                    float evdur = 0.5f + random::normal();
                    evdur = clamp(evdur,0.1,5.0);
                    int ampenv = random::u32() % m_numAmpEnvs;
                    m_voices[voiceIndex].start(evdur,-24.0f,24.0f,&m_amp_envelopes[ampenv]);
                    break;
                }
                ++i;
            }
            float density = 5.0f;
            m_nextEventPos += -log(random::uniform())/density;
        }
        for (int i=0;i<m_maxVoices;++i)
        {
            float gate = 0.0f;
            float amp = 0.0f;
            float pitch = 0.0f;
            float par1 = 0.0f;
            float par2 = 0.0f;
            if (m_voices[i].isAvailable()==false && m_phase>=m_voices[i].m_startPos)
            {
                m_voices[i].process(args.sampleTime,&gate,&pitch,&amp,&par1,&par2);
            }
            outputs[OUT_GATE+i].setVoltage(gate);
            pitch = pitch*(1.0f/12);
            outputs[OUT_PITCH+i].setVoltage(pitch);
            outputs[OUT_VCA+i].setVoltage(amp);
            outputs[OUT_AUX1+i].setVoltage(par1);
            outputs[OUT_AUX2+i].setVoltage(par2);
        }
        if (m_resetTrigger.process(rescale(inputs[IN_RESET].getVoltage(),0.0,10.0, 0.f, 1.f)))
        {
            m_nextEventPos = 0.0;
            m_phase = 0.0;
        }
        m_phase+=args.sampleTime;
    }
private:
    float m_phase = 0.0f;
    float m_nextEventPos = 0.0f;
    StocVoice m_voices[16];
    breakpoint_envelope m_amp_envelopes[6];
    int m_maxVoices = 6;
    dsp::SchmittTrigger m_resetTrigger;
};

class XStochasticWidget : public ModuleWidget
{
public:
    
    XStochasticWidget(XStochastic* m)
    {
        setModule(m);
        box.size.x = 600.0f;
        
        if (!g_font)
        	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
        for (int i=0;i<12;++i)
        {
            addOutput(createOutput<PJ301MPort>(Vec(5, 20+i*25), module, XStochastic::OUT_GATE+i));
            addOutput(createOutput<PJ301MPort>(Vec(30, 20+i*25), module, XStochastic::OUT_PITCH+i));
            addOutput(createOutput<PJ301MPort>(Vec(55, 20+i*25), module, XStochastic::OUT_VCA+i));
            addOutput(createOutput<PJ301MPort>(Vec(80, 20+i*25), module, XStochastic::OUT_AUX1+i));
            addOutput(createOutput<PJ301MPort>(Vec(105, 20+i*25), module, XStochastic::OUT_AUX2+i));
        }
        addInput(createInput<PJ301MPort>(Vec(5, 330), module, XStochastic::IN_RESET));
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
        nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);

        nvgFontSize(args.vg, 15);
        nvgFontFaceId(args.vg, g_font->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        nvgText(args.vg, 3 , 10, "GenDyn", NULL);
        nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
private:
    
};

Model* modelXStochastic = createModel<XStochastic, XStochasticWidget>("XStochastic");
