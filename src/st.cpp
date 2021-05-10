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

class STEnvelope
{
public:
    void start(int envIndex, float sampleRate, float length, bool attackReleaseFades)
    {
        mActiveEnvelope = envIndex;
        mSampleRate = sampleRate;
        mLen = length;
        mSamplePos = 0;
        mEnvIndex = 0;
        mNumEnvPoints = EnvelopeTable[envIndex][0];
        mFadesActive = attackReleaseFades;
    }
    float process()
    {
        if (mActiveEnvelope == -1)
            return 0.0f;
        float envValue = 0.0f;
        if (mNumEnvPoints == 1)
        {
            envValue = EnvelopeTable[mActiveEnvelope][1];
        }
        int envLenSamples = mSampleRate * mLen;
        if (mFadesActive)
        {
            float fadeGain = 1.0f;
            int fadeLenSamples = mSampleRate * mFadeLen;
            if (mSamplePos<fadeLenSamples)
                fadeGain = rescale(mSamplePos,0,fadeLenSamples,0.0f,1.0f);
            if (mSamplePos>envLenSamples-fadeLenSamples)
                fadeGain = rescale(mSamplePos,envLenSamples-fadeLenSamples,envLenSamples,1.0f,0.0f);
            envValue *= fadeGain;
        }
        return envValue;
    }
private:
    int mActiveEnvelope = -1;
    int mNumEnvPoints = 0;
    float mSampleRate = 0.0f;
    float mLen = 0.0f;
    bool mFadesActive = false;
    int mSamplePos = 0;
    int mEnvIndex = 0;
    float mFadeLen = 0.01;
};

class StocVoice
{
public:
    StocVoice()
    {
        m_pitch_env.AddNode({0.0f,0.0f});
        m_pitch_env.AddNode({1.0f,0.0f});
    }
    void process(float deltatime, float* gate,float* pitch,float* amp,float* par1, float* par2)
    {
        *gate = 10.0f;
        
        m_phase += deltatime;
        float normphase = 1.0f/m_len*m_phase;
        float gain = m_amp_env->GetInterpolatedEnvelopeValue(normphase);
        *amp = rescale(gain,0.0f,1.0f,0.0f,10.0f);
        *pitch = m_pitch + m_pitch_env.GetInterpolatedEnvelopeValue(normphase);
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
        float glissdest = rescale(rack::random::uniform(),0.0f,1.0f,-12.0f,12.0f);
        m_pitch_env.GetNodeAtIndex(1).pt_y = glissdest;
        m_available = false;
    }
    float m_playProb = 1.0f;
    float m_startPos = 0.0f;
private:
    bool m_available = true;
    breakpoint_envelope m_pitch_env;
    breakpoint_envelope* m_amp_env = nullptr;
    breakpoint_envelope* m_par1_env = nullptr;
    breakpoint_envelope* m_par2_env = nullptr;
    double m_phase = 0.0;
    double m_len = 0.5;
    float m_min_pitch = -24.0f;
    float m_max_pitch = 24.0f;
    float m_pitch = 0.0f;
    float m_glissrange = 0.0f;
};

class XStochastic : public rack::Module
{
public:
    enum OUTPUTS
    {
        ENUMS(OUT_GATE, 16),
        ENUMS(OUT_PITCH, 16),
        ENUMS(OUT_VCA, 16),
        ENUMS(OUT_AUX1, 16),
        ENUMS(OUT_AUX2, 16)
    };
    
    XStochastic()
    {
        m_amp_envelopes[0].AddNode({0.0,0.0});
        m_amp_envelopes[0].AddNode({0.5,1.0});
        m_amp_envelopes[0].AddNode({1.0,0.0});
        config(0,0,OUT_AUX2_LAST);
    }
    void process(const ProcessArgs& args) override
    {
        if (m_phase>=m_nextEventPos)
        {
            for (int i=0;i<16;++i)
            {
                if (m_voices[i].isAvailable())
                {
                    m_voices[i].m_startPos = m_nextEventPos;
                    float evdur = 0.5f + random::normal();
                    evdur = clamp(evdur,0.1,5.0);
                    m_voices[i].start(evdur,-24.0f,24.0f,&m_amp_envelopes[0]);
                    break;
                }
            }
            float density = 3.0f;
            m_nextEventPos += -log(random::uniform())/density;
        }
        for (int i=0;i<16;++i)
        {
            float gate = 0.0f;
            float amp = 0.0f;
            float pitch = 0.0f;
            if (m_voices[i].isAvailable()==false && m_phase>=m_voices[i].m_startPos)
            {
                m_voices[i].process(args.sampleTime,&gate,&pitch,&amp,nullptr,nullptr);
            }
            outputs[OUT_GATE+i].setVoltage(gate);
            pitch = pitch*(1.0f/12);
            outputs[OUT_PITCH+i].setVoltage(pitch);
            outputs[OUT_VCA+i].setVoltage(amp);
        }
        m_phase+=args.sampleTime;
    }
private:
    float m_phase = 0.0f;
    float m_nextEventPos = 0.0f;
    StocVoice m_voices[16];
    breakpoint_envelope m_amp_envelopes[6];
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
        }
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
