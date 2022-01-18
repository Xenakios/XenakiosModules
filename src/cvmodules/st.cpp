#include "../plugin.hpp"
#include <random>
#include <array>
#include "../jcdp_envelope.h"
#include "../helperwidgets.h"
#include <osdialog.h>

inline int randomDiscrete(std::mt19937& rng,std::vector<float>& whs)
{
    std::uniform_real_distribution<float> dist(0.0f,1.0f);
    float accum = 0.0f;
    for (int i=0;i<whs.size();++i)
      accum+=whs[i];
    // if all weights zero, just pick uniformly 
    if (accum==0.0f)
    {
        std::uniform_int_distribution<int> idist(0,whs.size()-1);
        return idist(rng);
    }
    float scaler = 1.0f/accum;
    float z = dist(rng);
    int choice = -1;
    accum = 0.0f;
    for (int i=0;i<whs.size();++i)
    {
        accum += whs[i] * scaler;
        if (accum>=z)
        {
            choice = i;
            break;
        }
        
    }
    return choice;
}

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
    dsp::ClockDivider cd;
    std::array<float,66> ampwarp_table;
    std::array<float,66> pitchwarp_table;
    StocVoice()
    {
        cd.setDivision(8);
        for (int i=0;i<ampwarp_table.size()-2;++i)
        {
            ampwarp_table[i] = rescale((float)i,0.0f,(float)ampwarp_table.size()-2,0.0f,1.0f);
            pitchwarp_table[i] = rescale((float)i,0.0f,(float)pitchwarp_table.size()-2,0.0f,1.0f);
        }
        ampwarp_table[ampwarp_table.size()-2] = 1.0f;
        ampwarp_table[ampwarp_table.size()-1] = 1.0f;
        pitchwarp_table[pitchwarp_table.size()-2] = 1.0f;
        pitchwarp_table[pitchwarp_table.size()-1] = 1.0f;
        m_amp_resp_smoother.setAmount(0.95);
        
        m_pitch_env.AddNode({0.0f,0.0f,2});
        m_pitch_env.AddNode({1.0f,0.0f,2});

        m_par1_env.AddNode({0.0f,0.0f,2});
        m_par1_env.AddNode({1.0f,0.0f,2});

        m_par2_env.AddNode({0.0f,0.0f,2});
        m_par2_env.AddNode({1.0f,0.0f,2});

        m_par3_env.AddNode({0.0f,0.0f,2});
        m_par3_env.AddNode({1.0f,0.0f,2});

        for (int i=0;i<m_activeOuts.size();++i)
        {
            m_activeOuts[i] = false;
            m_Outs[i] = 0.0f;
        }
        
        m_quanScale.resize(4096);
    }
    void setScale(std::vector<double> sc)
    {
        m_scaleToChangeTo = sc;
        m_doScaleChange = true;
    }
    void process(float deltatime)
    {
        float normphase = 1.0f/m_len*m_phase;
        if (normphase<0.5f)
            m_Outs[0] = 10.0f;
        else
            m_Outs[0] = 0.0f;
        //if (cd.process())
        {
        if (m_activeOuts[2])
        {
            float aenvphase = interpolateLinear(ampwarp_table.data(), normphase*64);
            /*
            if (m_amp_env_warp<0.0f)
                aenvphase = 1.0f-std::pow(1.0f-normphase,rescale(m_amp_env_warp,-1.0f,0.0f,1.0f,4.0f));
            else if (m_amp_env_warp == 0.0f)
                aenvphase = normphase;
            else
                aenvphase = std::pow(normphase,rescale(m_amp_env_warp,0.0f,1.0f,1.0f,4.0f));
            */
            float gain = m_amp_env->GetInterpolatedEnvelopeValue(aenvphase);
            m_Outs[2] = rescale(gain,0.0f,1.0f,0.0f,10.0f);
        }
        
        if (m_activeOuts[1] && cd.process())
        {
            float penvphase = normphase;
            /*
            if (m_pitch_env_warp<0.0f)
                penvphase = 1.0f-std::pow(1.0f-normphase,rescale(m_pitch_env_warp,-1.0f,0.0f,1.0f,4.0f));
            else
                penvphase = std::pow(normphase,rescale(m_pitch_env_warp,0.0f,1.0f,1.0f,4.0f));
            */
            if (m_doScaleChange == true)
            {
                m_quanScale = m_scaleToChangeTo;
                m_doScaleChange = false;
            }
            float penvvalue = m_pitch_env.GetInterpolatedEnvelopeValue(penvphase);
            float qpitch = m_pitch;
            if (mPitchQAmount>0.0f)
                qpitch = quantize_to_grid(m_pitch,m_quanScale,mPitchQAmount);
            m_Outs[1] = reflect_value<float>(-60.0f,qpitch + penvvalue,60.0f);
        }
        if (m_activeOuts[3])
        {
            m_Outs[3] = reflect_value<float>(-5.0f,m_par1 + m_par1_env.GetInterpolatedEnvelopeValue(normphase),5.0f);
        }
        if (m_activeOuts[4])
        {
            m_Outs[4] = reflect_value<float>(-5.0f,m_par2 + m_par2_env.GetInterpolatedEnvelopeValue(normphase),5.0f);
        }
        if (m_activeOuts[5])
        {
            if (m_chaosphase>=(1.0/m_chaos_rate))
            {
                double chaos_r = 2.9+1.1*m_chaos_amt;
                double chaos = chaos_r * (1.0-m_chaos) * m_chaos;
                
                m_chaos = chaos;
                m_chaosphase = 0.0;
            }
            m_chaosphase += deltatime;
            m_Outs[5] = rescale(m_chaos_smoother.process(m_chaos),0.0,1.0,-5.0f,5.0f);
        }
        }
        m_phase += deltatime;
        if (m_phase>=m_len)
        {
            m_available = true;
        }
    }
    float mPitchQAmount = 1.0f;
    void setPitchQuantAount(float a)
    {
        mPitchQAmount = clamp(a,0.0f,1.0f);
    }
    bool isAvailable()
    {
        return m_available;
    }
    std::vector<double> m_quanScale;
    std::vector<double> m_scaleToChangeTo;
    std::atomic<bool> m_doScaleChange{false};
    void start(float dur, float centerpitch,float spreadpitch, breakpoint_envelope* ampenv,
        float glissprob, float gliss_spread, int penv, float aenvwspr, float penvwspr)
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
            int shap = penv;
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
        m_par3 = rescale(dist(*m_rng),0.0f,1.0f,-5.0f,5.0f);
        float trip = dist(*m_rng);
        trip = quantize(trip,1.0/3,1.0f);
        pardest = rescale(trip,0.0f,1.0f,-5.0f,5.0f);
        m_par3_env.GetNodeAtIndex(1).pt_y = pardest;
        m_available = false;
        float spar = std::pow(m_chaos_smooth,0.3);
        m_chaos_smoother.setAmount(rescale(spar,0.0f,1.0f,0.9f,0.9999f));
        
        m_amp_env_warp = normdist(*m_rng) * aenvwspr * 0.5f;
        m_amp_env_warp = clamp(m_amp_env_warp,-1.0f,1.0f);

        m_pitch_env_warp = normdist(*m_rng) * penvwspr * 0.5f;
        m_pitch_env_warp = clamp(m_pitch_env_warp,-1.0f,1.0f);
    }
    void reset()
    {
        m_available = true;
        m_phase = 0.0;
    }
    float m_playProb = 1.0f;
    float m_startPos = 0.0f;
    std::mt19937* m_rng = nullptr;
    std::array<bool,6> m_activeOuts;
    std::array<float,6> m_Outs;
    double m_chaos_amt = 0.0;
    double m_chaos_rate = 1.0;
    double m_chaos_smooth = 0.0;
    double m_chaos = 0.417;
    OnePoleFilter m_amp_resp_smoother;
private:
    bool m_available = true;
    breakpoint_envelope m_pitch_env;
    breakpoint_envelope* m_amp_env = nullptr;
    breakpoint_envelope m_par1_env;
    breakpoint_envelope m_par2_env;
    breakpoint_envelope m_par3_env;
    double m_phase = 0.0;
    double m_len = 0.5;
    float m_min_pitch = -24.0f;
    float m_max_pitch = 24.0f;
    float m_pitch = 0.0f;
    float m_glissrange = 0.0f;
    float m_par1 = 0.0f;
    float m_par2 = 0.0f;
    float m_par3 = 0.0f;
    float m_amp_env_warp = 0.0f;
    float m_pitch_env_warp = 0.0f;
    double m_chaosphase = 0;
    OnePoleFilter m_chaos_smoother;
};

class XStochastic : public rack::Module
{
public:
    enum INPUTS
    {
        IN_RESET,
        IN_PITCH_CENTER,
        IN_RATE,
        IN_PITCH_SPREAD,
        IN_GLISS_PROB,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_GATE,
        OUT_PITCH,
        OUT_VCA,
        OUT_AUX1, 
        OUT_AUX2,
        OUT_AUX3,
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
        PAR_PITCHSPREAD_CV,
        PAR_GLISSPROB_CV,
        PAR_MASTER_DURDEV,
        PAR_AUX3_CHAOS,
        PAR_AUX3_CHAOS_RATE,
        PAR_AUX3_CHAOS_SMOOTH,
        PAR_AMP_ENV_WARP_SPREAD,
        PAR_PITCH_ENV_WARP_SPREAD,
        ENUMS(PAR_DISPLAY_WEIGHT,16),
        ENUMS(PAR_DISPLAY_WEIGHT2,16),
        PAR_PITCHQUANAMOUNT,
        PAR_LAST
    };
    int m_numAmpEnvs = 11;
    int m_lastAllocatedVoice = 0;
    std::string mScalaFilename;
    void loadScaleFromFile(std::string fn)
    {
        if (fn==mScalaFilename)
            return;
        mScalaFilename = fn;
        auto thescale = Tunings::readSCLFile(fn);
        auto sc = semitonesFromScalaScale<double>(thescale,-60.0,60.0);
        for (int i=0;i<16;++i)
        {
            m_voices[i].m_rng = &m_rng;
            m_voices[i].m_chaos = 0.417+0.2/16*i;
            m_voices[i].setScale(sc);
        }
    }
    XStochastic()
    {
        std::string dir = asset::plugin(pluginInstance, "res/scala_scales");
        std::string fn = dir+"/pure fifths.scl";
        loadScaleFromFile(fn);
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

        m_pitch_amp_response.AddNode({-48.0f,0.0f,2});
        m_pitch_amp_response.AddNode({-36.0f,1.0f,2});
        //m_pitch_amp_response.AddNode({0.0f,1.0f,2});
        //m_pitch_amp_response.AddNode({1.0f,0.1f,2});
        //m_pitch_amp_response.AddNode({12.0f,0.1f,2});
        m_pitch_amp_response.AddNode({24.0f,1.0f,2});
        m_pitch_amp_response.AddNode({48.0f,0.05f,2});
        
        ampEnvWhs.resize(16);
        pitchEnvWhs.resize(16);

        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_MASTER_MEANDUR,0.1,2.0,0.5,"Master mean duration");
        configParam(PAR_MASTER_GLISSPROB,0.0,1.0,0.5,"Master glissando probability");
        configParam(PAR_MASTER_DENSITY, -2.f, 6.f, 1.f, "Master density", " events per second", 2, 1);
        configParam(PAR_MASTER_RANDSEED,0.0,512.0,256.0,"Master random seed");
        configParam(PAR_MASTER_GLISS_SPREAD,-1.0,1.0,0.2,"Master glissando spread");
        configParam(PAR_MASTER_PITCH_CENTER,-48.0,48.0,0.0,"Master pitch center");
        configParam(PAR_MASTER_PITCH_SPREAD,0,48.0,12.0,"Master pitch spread");
        configParam(PAR_NUM_OUTPUTS,1,16.0,8.0,"Number of outputs");
        configParam(PAR_MASTER_PITCH_ENV_TYPE,0,msnumtables,0.0,"Pitch envelope type");
        configParam(PAR_MASTER_AMP_ENV_TYPE,0,m_numAmpEnvs,0.0,"VCA envelope type");
        configParam(PAR_RATE_CV,-1.0f,1.0f,0.0,"Master density CV ATTN");
        configParam(PAR_RATE_QUAN_STEP,-2.0f,5.0f,2.0f,"Rate quantization step", " Hz",2,1);
        configParam(PAR_RATE_QUAN_AMOUNT,0.0f,1.0f,0.0,"Rate quantization amount");
        configParam(PAR_PITCHSPREAD_CV,-1.0f,1.0f,0.0,"Pitch spread CV ATTN");
        configParam(PAR_GLISSPROB_CV,-1.0f,1.0f,0.0,"Gliss probability CV ATTN");
        configParam(PAR_MASTER_DURDEV,0.0f,1.0f,1.0,"Duration spread");
        configParam(PAR_AUX3_CHAOS,0.0f,1.0f,0.0,"AUX3 chaos amount");
        configParam(PAR_AUX3_CHAOS_RATE,-3.0f,10.0f,1.0,"AUX3 chaos rate", " Hz",2,1);
        configParam(PAR_AUX3_CHAOS_SMOOTH,0.0f,1.0f,0.0,"AUX3 chaos smooth");
        configParam(PAR_AMP_ENV_WARP_SPREAD,0.0f,1.0f,0.0,"Amplitude envelope warp spread");
        configParam(PAR_PITCH_ENV_WARP_SPREAD,0.0f,1.0f,0.0,"Gliss envelope warp spread");
        for (int i=0;i<16;++i)
        {
            configParam(PAR_DISPLAY_WEIGHT+i,0.0f,1.0f,1.0,"Envelope selection weight "+std::to_string(i));
            configParam(PAR_DISPLAY_WEIGHT2+i,0.0f,1.0f,1.0,"Envelope selection weight2 "+std::to_string(i));
        }
        configParam(PAR_PITCHQUANAMOUNT,0.0f,1.0f,1.0f,"Pitch quantization amount");
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
            glissprob += inputs[IN_GLISS_PROB].getVoltage() * 0.1f * params[PAR_GLISSPROB_CV].getValue();
            glissprob = clamp(glissprob,0.0f,1.0f);
            float gliss_spread = params[PAR_MASTER_GLISS_SPREAD].getValue();
            float meandur = params[PAR_MASTER_MEANDUR].getValue();
            float density = params[PAR_MASTER_DENSITY].getValue();
            density += inputs[IN_RATE].getVoltage()*params[PAR_RATE_CV].getValue();
            density = clamp(density,-3.0f,5.0f);
            density = std::pow(2.0f,density);
            float durdev = params[PAR_MASTER_DURDEV].getValue()*(1.0f/density);
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
            spreadpitch += inputs[IN_PITCH_SPREAD].getVoltage() * 4.8f * params[PAR_PITCHSPREAD_CV].getValue();
            spreadpitch = clamp(spreadpitch,0.0f,48.0f);
            int manual_amp_env = params[PAR_MASTER_AMP_ENV_TYPE].getValue();
            for (int i=0;i<m_numAmpEnvs;++i)
                ampEnvWhs[i] = params[PAR_DISPLAY_WEIGHT+i].getValue();
            manual_amp_env = randomDiscrete(m_rng,ampEnvWhs);
            if (manual_amp_env>=m_numAmpEnvs)
                manual_amp_env = 0;
            //int manual_pitch_env = params[PAR_MASTER_PITCH_ENV_TYPE].getValue();
            for (int i=0;i<msnumtables;++i)
                pitchEnvWhs[i] = params[PAR_DISPLAY_WEIGHT2+i].getValue();
            int manual_pitch_env = randomDiscrete(m_rng,pitchEnvWhs);
            if (manual_pitch_env>=msnumtables)
                manual_pitch_env = 0;
            float aenvwarp = params[PAR_AMP_ENV_WARP_SPREAD].getValue();
            float pitchenvwarp = params[PAR_PITCH_ENV_WARP_SPREAD].getValue();
            
            for (int i=0;i<numvoices;++i)
            {
                int voiceIndex = (m_lastAllocatedVoice+i) % numvoices;
                if (m_voices[voiceIndex].isAvailable())
                {
                    m_voices[voiceIndex].m_startPos = m_nextEventPos;
                    float evdur = meandur + durdist(m_rng)*durdev;
                    evdur = clamp(evdur,0.05,8.0);
                    //int ampenv = manual_amp_env - 1;
                    //if (ampenv < 0)
                    //    ampenv = vcadist(m_rng);
                    int ampenv = manual_amp_env;
                    m_voices[voiceIndex].m_chaos_smooth = params[PAR_AUX3_CHAOS_SMOOTH].getValue();
                    m_voices[voiceIndex].start(evdur,centerpitch,spreadpitch,
                        &m_amp_envelopes[ampenv],glissprob,gliss_spread,manual_pitch_env,
                        aenvwarp,pitchenvwarp);
                    ++m_eventCounter;
                    m_lastAllocatedVoice = voiceIndex;
                    break;
                }
            }
            
            double qamt = params[PAR_RATE_QUAN_AMOUNT].getValue();
            double deltatime = -log(dist(m_rng))/density;
            deltatime = clamp(deltatime,args.sampleTime,30.0f);
            double evpos = m_nextEventPos + deltatime;
            float qstep = std::pow(2.0f,params[PAR_RATE_QUAN_STEP].getValue());
            evpos = quantize(evpos,1.0f/qstep,qamt);
            if ((evpos-m_phase)<0.002)
            {
                evpos = m_phase + 0.002;
                //++m_eventCounter;
            }
            //++m_eventCounter;
            m_nextEventPos = evpos;
            for (int i=0;i<numvoices;++i)
            {
                m_voices[i].m_activeOuts[0] = outputs[OUT_GATE].isConnected();
                m_voices[i].m_activeOuts[1] = outputs[OUT_PITCH].isConnected();
                m_voices[i].m_activeOuts[2] = outputs[OUT_VCA].isConnected();
                m_voices[i].m_activeOuts[3] = outputs[OUT_AUX1].isConnected();
                m_voices[i].m_activeOuts[4] = outputs[OUT_AUX2].isConnected();
                m_voices[i].m_activeOuts[5] = outputs[OUT_AUX3].isConnected();
            }
            //m_nextEventPos += deltatime;
        }
        m_NumUsedVoices = 0;
        outputs[OUT_PITCH].setChannels(numvoices);
        outputs[OUT_GATE].setChannels(numvoices);
        outputs[OUT_VCA].setChannels(numvoices);
        outputs[OUT_AUX1].setChannels(numvoices);
        outputs[OUT_AUX2].setChannels(numvoices);
        outputs[OUT_AUX3].setChannels(numvoices);
        double chaos_amt = params[PAR_AUX3_CHAOS].getValue();
        double chaos_rate = std::pow(2.0,params[PAR_AUX3_CHAOS_RATE].getValue());
        float pqamt = params[PAR_PITCHQUANAMOUNT].getValue();
        for (int i=0;i<numvoices;++i)
        {
            std::array<float,6> vouts{0.0f,0.0f,0.0f,0.0f,0.0f,0.0f};
            if (m_voices[i].isAvailable()==false && m_phase>=m_voices[i].m_startPos)
            {
                m_voices[i].m_chaos_amt = chaos_amt;
                m_voices[i].m_chaos_rate = chaos_rate;
                m_voices[i].setPitchQuantAount(pqamt);
                m_voices[i].process(args.sampleTime);
                
                //float aresp = m_pitch_amp_response.GetInterpolatedEnvelopeValue(vouts[1]); 
                //vouts[2] *= m_voices[i].m_amp_resp_smoother.process(aresp); 
                ++m_NumUsedVoices;
            }
            vouts = m_voices[i].m_Outs;
            outputs[OUT_GATE].setVoltage(vouts[0],i);
            outputs[OUT_PITCH].setVoltage(vouts[1]*(1.0f/12),i);
            outputs[OUT_VCA].setVoltage(vouts[2],i);
            outputs[OUT_AUX1].setVoltage(vouts[3],i);
            outputs[OUT_AUX2].setVoltage(vouts[4],i);
            outputs[OUT_AUX3].setVoltage(vouts[5],i);
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
    json_t* dataToJson() 
    {
        json_t* resultJ = json_object();
        json_object_set(resultJ,"scalafile",json_string(mScalaFilename.c_str()));
        return resultJ;
    }
    void dataFromJson(json_t* root)
    {
        if (!root)
            return;
        auto sj = json_object_get(root,"scalafile");
        if (sj)
        {
            std::string fn(json_string_value(sj));
            loadScaleFromFile(fn);
        }
    }
    unsigned int m_randSeed = 1;
    std::vector<float> ampEnvWhs;
    std::vector<float> pitchEnvWhs;
    breakpoint_envelope m_amp_envelopes[16];
private:
    double m_phase = 0.0f;
    double m_nextEventPos = 0.0f;
    std::mt19937 m_rng{m_randSeed};
    StocVoice m_voices[16];
    
    breakpoint_envelope m_pitch_amp_response;
    dsp::SchmittTrigger m_resetTrigger;
};

class MyTrimpot : public Trimpot
{
public:
    MyTrimpot() {}
    void onEnter(const event::Enter& e) override
    {
        Trimpot::onEnter(e);
        if (EnterCallback)
            EnterCallback(envType,env);
    }
    void onLeave(const event::Leave& e) override
    {
        Trimpot::onLeave(e);
        if (EnterCallback)
            EnterCallback(-1,-1);
    }
    std::function<void(int,int)> EnterCallback;
    int envType = -1;
    int env = -1;
};

class STEnvelopesWidget : public TransparentWidget
{
public:
    XStochastic* m_st = nullptr;
    int m_envType = -1;
    int m_env = -1;
    breakpoint_envelope m_pitchenv;
    STEnvelopesWidget(XStochastic* m) : m_st(m)
    {
        m_pitchenv.AddNode({0.0f,0.0f});
        m_pitchenv.AddNode({1.0f,1.0f});
    }
    void draw(const DrawArgs &args) override
    {
        if (m_st==nullptr)
            return;
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg,nvgRGBA(0x00, 0x00, 0x00, 0xff));
        nvgRect(args.vg,0.0f,0.0f,box.size.x,box.size.y);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg,nvgRGBA(0xff, 0xff, 0xff, 0xff));
        breakpoint_envelope* env = nullptr;
        if (m_envType == 0 && m_env>=0 && m_env<m_st->m_numAmpEnvs)
            env = &m_st->m_amp_envelopes[m_env];
        if (m_envType == 1)
        {
            env = &m_pitchenv;
            auto& pt = env->GetNodeAtIndex(0);
            pt.Shape = m_env;
        }
        if (env)
        {
            nvgBeginPath(args.vg);
            int w = box.size.x;
            for (int i=0;i<w;++i)
            {
                float xcor = (float)i;
                float norm = rescale((float)i,0.0f,w,0.0f,1.0f);
                float ycor = rescale(env->GetInterpolatedEnvelopeValue(norm),0.0f,1.0f,box.size.y,0.0f);
                if (i == 0)
                    nvgMoveTo(args.vg,xcor,ycor);
                else
                    nvgLineTo(args.vg,xcor,ycor);
            }
            nvgStroke(args.vg);
        }
        nvgRestore(args.vg);
    }
    void setEnvelopeToShow(int a, int b)
    {
        m_envType = a;
        m_env = b;
    }
};

class XStochasticWidget : public ModuleWidget
{
public:
    int hovEtype = -1;
    int hovE = -1;
    STEnvelopesWidget* m_ew = nullptr;
    XStochasticWidget(XStochastic* m)
    {
        setModule(m);
        box.size.x = RACK_GRID_WIDTH*28;
        
        float xc = 1;
        float yc = 15;
        PortWithBackGround* port = nullptr;
        port = new PortWithBackGround(m,this,XStochastic::OUT_GATE,xc,yc,"GATE",true);
        xc = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,XStochastic::OUT_PITCH,xc,yc,"PITCH",true);
        xc = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,XStochastic::OUT_VCA,xc,yc,"GAIN",true);
        xc = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,XStochastic::OUT_AUX1,xc,yc,"AUX 1",true);
        xc = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,XStochastic::OUT_AUX2,xc,yc,"AUX 2",true);
        xc = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,XStochastic::OUT_AUX3,xc,yc,"AUX 3",true);
        xc = port->box.getRight()+2;
        addParam(createParam<Trimpot>(Vec(xc, yc), m, XStochastic::PAR_AUX3_CHAOS));    
        addParam(createParam<Trimpot>(Vec(xc, yc+21), m, XStochastic::PAR_AUX3_CHAOS_RATE));    
        addParam(createParam<Trimpot>(Vec(xc+21, yc), m, XStochastic::PAR_AUX3_CHAOS_SMOOTH));    
        
        addInput(createInput<PJ301MPort>(Vec(xc+42, yc), module, XStochastic::IN_RESET));
        float lfs = 9.0f;
        xc = 2.0f;
        yc = 60.0f;
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
            XStochastic::IN_PITCH_SPREAD,XStochastic::PAR_PITCHSPREAD_CV,xc,yc));
        xc = 2;
        yc += 47;
        addChild(new KnobInAttnWidget(this,"GLISS PROBABILITY",XStochastic::PAR_MASTER_GLISSPROB,
            XStochastic::IN_GLISS_PROB,XStochastic::PAR_GLISSPROB_CV,xc,yc));
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
            -1,-1,xc,yc,false,8.0f));
        xc = 2;
        yc += 47;
        addChild(new KnobInAttnWidget(this,"DURATION SPREAD",XStochastic::PAR_MASTER_DURDEV,
            -1,-1,xc,yc,false,8.0f));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"AMP ENV WARP SPR",XStochastic::PAR_AMP_ENV_WARP_SPREAD,
            -1,-1,xc,yc,false,8.0f));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"GLISS ENV WARP SPR",XStochastic::PAR_PITCH_ENV_WARP_SPREAD,
            -1,-1,xc,yc,false,8.0f));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"PITCH QUANT",XStochastic::PAR_PITCHQUANAMOUNT,
            -1,-1,xc,yc,false,8.0f));
        yc += 47;
        syc = yc;
        auto knobcb = [this](int a, int b)
        {
            if (a == -1)
                m_ew->hide();
            else
            {
                m_ew->show();
                m_ew->setEnvelopeToShow(a,b);
            }
        };
        for (int i=0;i<16;++i)
        {
            MyTrimpot* mp = nullptr;
            addParam(mp = createParam<MyTrimpot>(Vec(1+20*i, yc), module, XStochastic::PAR_DISPLAY_WEIGHT+i));
            mp->envType = 0;
            mp->env = i;
            mp->EnterCallback =  knobcb; //[this](int a, int b){ hovEtype = a; hovE = b; };
            addParam(mp = createParam<MyTrimpot>(Vec(1+20*i, yc+20), module, XStochastic::PAR_DISPLAY_WEIGHT2+i));
            mp->envType = 1;
            mp->env = i;
            mp->EnterCallback = knobcb; // [this](int a, int b){ hovEtype = a; hovE = b; };
        }
        pitchenv.AddNode({0.0f,0.0f});
        pitchenv.AddNode({1.0f,1.0f});
        m_ew = new STEnvelopesWidget(m);
        m_ew->box.pos = {0,60};
        m_ew->box.size = {box.size.x,185};
        addChild(m_ew);
        m_ew->hide();
    }
    int syc = 0;
    ~XStochasticWidget()
    {
        
    }
    void appendContextMenu(Menu* menu) override
    {
        XStochastic* sm = dynamic_cast<XStochastic*>(module);
        menu->addChild(createMenuItem([sm]()
        {
            for (int i=0;i<16;++i)
                sm->params[XStochastic::PAR_DISPLAY_WEIGHT+i].setValue(0.0f);
        },"Set amp envelope probabilities to 0"));
        menu->addChild(createMenuItem([sm]()
        {
            for (int i=0;i<16;++i)
                sm->params[XStochastic::PAR_DISPLAY_WEIGHT+i].setValue(1.0f);
        },"Set amp envelope probabilities to 1"));
        menu->addChild(createMenuItem([sm]()
        {
            for (int i=0;i<16;++i)
                sm->params[XStochastic::PAR_DISPLAY_WEIGHT2+i].setValue(0.0f);
        },"Set pitch envelope probabilities to 0"));
        menu->addChild(createMenuItem([sm]()
        {
            for (int i=0;i<16;++i)
                sm->params[XStochastic::PAR_DISPLAY_WEIGHT2+i].setValue(1.0f);
        },"Set pitch envelope probabilities to 1"));
        menu->addChild(createMenuItem([sm]()
        {
            
                std::string dir = asset::plugin(pluginInstance, "/res");
                osdialog_filters* filters = osdialog_filters_parse("Scala File:scl");
                char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
                osdialog_filters_free(filters);
                if (!pathC) {
                    return;
                }
                std::string path = pathC;
                std::free(pathC);
                sm->loadScaleFromFile(path);
        },"Load Scala tuning file..."));   
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
        nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        nvgText(args.vg, 3 , 10, "ST(ochastic)", NULL);
        char buf[200];
        XStochastic* sm = dynamic_cast<XStochastic*>(module);
        if (sm)
            sprintf(buf,"Xenakios %d voices, %d events %d %d",sm->m_NumUsedVoices,sm->m_eventCounter,hovEtype,hovE);
        else sprintf(buf,"Xenakios");
        nvgText(args.vg, 3 , h-11, buf, NULL);
        
        
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
private:
    breakpoint_envelope pitchenv;
};

Model* modelXStochastic = createModel<XStochastic, XStochasticWidget>("XStochastic");
