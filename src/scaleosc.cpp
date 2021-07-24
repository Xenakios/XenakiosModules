#include "plugin.hpp"
#include "helperwidgets.h"
#include "wtosc.h"
#include <array>
#include "jcdp_envelope.h"

float g_balance_tables[6][17];

inline void quantize_to_scale(float x, const std::vector<float>& g,
    float& out1, float& out2, float& outdiff)
{
    auto t1=std::upper_bound(std::begin(g),std::end(g),x);
    if (t1<std::end(g)-1)
    {
        auto t0=std::begin(g);
        if (t1>std::begin(g))
            t0=t1-1;
        out1 = *t0;
        out2 = *t1;
        if (out1 == out2)
        {
            outdiff = 0.0f;
            return;
        }
        outdiff = rescale(x,out1,out2,0.0,1.0);
        
        return;
    }
    out1 = g.back();
    out2 = g.back();
    outdiff = 1.00;
}


class SIMDSimpleOsc
{
public:
    SIMDSimpleOsc()
    {
        
    }
    simd::float_4 m_phase = 0.0;
    simd::float_4 m_phase_inc = 0.0;
    double m_samplerate = 44100;
    float m_warp = 0.0f;
    int m_warp_mode = 2;
    void prepare(int numchans, double samplerate)
    {
        m_samplerate = samplerate;
    }
    void setFrequencies(float hz0, float hz1)
    {
        simd::float_4 hzs(hz0,hz1,hz0,hz1);
        m_phase_inc = 1.0/m_samplerate*hzs;
    }
    void setFrequency(float hz)
    {
        simd::float_4 hzs(hz);
        m_phase_inc = 1.0/m_samplerate*hzs;
    }
    float m_warp_steps = 128.0f;
    void setPhaseWarp(int mode, float amt)
    {
        amt = clamp(amt,0.0f,1.0f);
        m_warp_mode = mode;
        m_warp_steps = std::pow(2.0f,2.0f+(1.0f-amt)*6.0f);
        m_warp = std::pow(amt,2.0f);
    }
    simd::float_4 processSample(float)
    {
        double phase_to_use;
#ifdef FOOSIMD
        if (m_warp_mode == 0)
        {
            phase_to_use = 1.0+m_warp*7.0;
            phase_to_use = m_phase * phase_to_use;
            if (phase_to_use>1.0)
                phase_to_use = 1.0f;
        } else if (m_warp_mode == 1)
        {
            double steps = m_warp_steps; // std::pow(2.0f,2.0f+(1.0-m_warp)*6.0f);
            /*
            double skewp = 0.05;
            if (m_warp<skewp)
            {
                steps = rescale(m_warp,0.00f,skewp,128.0f,16.0f);
            } else
            {
                steps = rescale(m_warp,skewp,1.0f,16.0f,3.0f);
                
            }
            */
            phase_to_use = std::round(m_phase*steps)/steps;
        }
        else
        {
            double pmult = rescale(m_warp,0.0f,1.0f,0.5f,2.0f);
            phase_to_use = std::fmod(pmult*m_phase,1.0);
        }
#endif
        simd::float_4 rs = simd::sin(simd::float_4(2*3.14159265359)*m_phase);
        //float r = std::sin(2*3.14159265359*phase_to_use);
        m_phase += m_phase_inc;
        //m_phase = std::fmod(m_phase,1.0);
        //m_phase = wrap_value(0.0,m_phase,1.0);
        m_phase = simd::fmod(m_phase,simd::float_4(1.0f));
        return rs;
    }
};


class SimpleOsc
{
public:
    SimpleOsc()
    {

    }
    double m_phase = 0.0;
    double m_phase_inc = 0.0;
    double m_samplerate = 44100;
    float m_warp = 0.0f;
    int m_warp_mode = 2;
    void prepare(int numchans, double samplerate)
    {
        m_samplerate = samplerate;
    }
    void setFrequency(float hz)
    {
        //if (hz<0.01f)
        //    hz = 0.01f;
        m_phase_inc = 1.0/m_samplerate*hz;
    }
    float m_warp_steps = 128.0f;
    void setPhaseWarp(int mode, float amt)
    {
        amt = clamp(amt,0.0f,1.0f);
        m_warp_mode = mode;
        m_warp_steps = std::pow(2.0f,2.0f+(1.0f-amt)*6.0f);
        m_warp = std::pow(amt,2.0f);
    }
    float processSample(float)
    {
        double phase_to_use;
        if (m_warp_mode == 0)
        {
            /*
            if (m_warp!=0.5f)
            {
                double w;
                if (m_warp<0.5)
                    w = rescale(m_warp,0.0f,0.5,0.2f,1.0f);
                else
                    w = rescale(m_warp,0.5f,1.0f,1.0f,4.0f);
                phase_to_use = std::pow(m_phase,w);
            } else
                phase_to_use = m_phase;
            */
            phase_to_use = 1.0+m_warp*7.0;
            phase_to_use = m_phase * phase_to_use;
            if (phase_to_use>1.0)
                phase_to_use = 1.0f;
        } else if (m_warp_mode == 1)
        {
            double steps = m_warp_steps; // std::pow(2.0f,2.0f+(1.0-m_warp)*6.0f);
            /*
            double skewp = 0.05;
            if (m_warp<skewp)
            {
                steps = rescale(m_warp,0.00f,skewp,128.0f,16.0f);
            } else
            {
                steps = rescale(m_warp,skewp,1.0f,16.0f,3.0f);
                
            }
            */
            phase_to_use = std::round(m_phase*steps)/steps;
        }
        else
        {
            double pmult = rescale(m_warp,0.0f,1.0f,0.5f,2.0f);
            phase_to_use = std::fmod(pmult*m_phase,1.0);
        }
        float r = std::sin(2*3.14159265359*phase_to_use);
        m_phase += m_phase_inc;
        //m_phase = std::fmod(m_phase,1.0);
        m_phase = wrap_value(0.0,m_phase,1.0);
        //if (m_phase>=1.0f)
        //    m_phase-=m_phase_inc;
        return r;
    }
};



class ScaleOscillator
{
public:
    ScaleOscillator()
    {
        auto calcbalancetable = [](breakpoint_envelope&e, int table)
        {
            for (int i=0;i<17;++i)
            {
                float g = e.GetInterpolatedEnvelopeValue(rescale((float)i,0,15,0.0,1.0));
                g_balance_tables[table][i] = g;
            }
        };
        breakpoint_envelope e;
        e.AddNode({0.0,1.0});
        e.AddNode({0.01,0.0});
        e.AddNode({1.0,0.0});
        calcbalancetable(e,0);
        e.ClearAllNodes();
        e.AddNode({0.0,1.0});
        //e.AddNode({0.5,0.1});
        e.AddNode({1.0,0.1});
        calcbalancetable(e,1);
        e.ClearAllNodes();
        e.AddNode({0.0,1.0});
        e.AddNode({1.0,1.0});
        calcbalancetable(e,2);
        e.ClearAllNodes();
        e.AddNode({0.0,1.0});
        //e.AddNode({0.5,1.0});
        e.AddNode({1.0,1.0});
        calcbalancetable(e,3);
        e.ClearAllNodes();
        e.AddNode({0.0,0.0});
        e.AddNode({0.99,0.0});
        e.AddNode({1.0,1.0});
        calcbalancetable(e,4);
        calcbalancetable(e,5);

        for (int i=0;i<m_oscils.size();++i)
        {
            //m_oscils[i].initialise([](float x){ return sin(x); },4096);
            m_oscils[i].prepare(1,44100.0f);
            m_osc_gain_smoothers[i*2+0].setAmount(0.9999);
            m_osc_gain_smoothers[i*2+1].setAmount(0.9999);
            m_osc_gains[i*2+0] = 1.0f;
            m_osc_gains[i*2+1] = 1.0f;
            m_osc_freqs[i*2+0] = 440.0f;
            m_osc_freqs[i*2+1] = 440.0f;
            fms[i] = 0.0f;
        }
        m_norm_smoother.setAmount(0.999);
        m_balance_smoother.setAmount(0.999);
        std::array<float,7> ratios{81.0f/80.0f,9.0f/8.0f,1.25f,1.333333f,1.5f,9.0f/5.0f,2.0f};
        double root_freq = dsp::FREQ_C4/16.0;
        for (int i=0;i<ratios.size();++i)
        {
            double freq = root_freq;
            std::vector<float> scale;
            while (freq<21000.0)
            {
                scale.push_back(freq);
                freq *= ratios[i];
            }
            m_scale_bank.push_back(scale);
        }
        std::vector<std::string> scalafiles;
        std::string dir = asset::plugin(pluginInstance, "res/scala_scales");
        scalafiles.push_back(dir+"/penta_opt.scl");
        scalafiles.push_back(dir+"/Ancient Greek Archytas Enharmonic.scl");
        scalafiles.push_back(dir+"/Chopi Xylophone.scl");
        scalafiles.push_back(dir+"/equally tempered minor.scl");
        scalafiles.push_back(dir+"/bohlen_quintuple_j.scl");
        
        for (auto& e : scalafiles)
        {
            auto pitches = loadScala(e,true,0.0,128);
            std::vector<float> scale;
            for (int i=0;i<pitches.size();++i)
            {
                double p = pitches[i]; //rescale(scale[i],-5.0f,5.0f,0.0f,120.0f);
                double freq = root_freq * std::pow(1.05946309436,p);
                scale.push_back(freq);
            }
            m_scale_bank.push_back(scale);
        }
        double freq = root_freq;
        std::vector<float> scale;
        int i=1;
        while (freq<20000.0)
        {
            freq = i * root_freq;
            scale.push_back(freq);
            ++i;
        }
        m_scale_bank.push_back(scale);
        m_scale.reserve(2048);
        m_scale = m_scale_bank[0];
        
        updateOscFrequencies();
    }
    void updateOscFrequencies()
    {
        double maxpitch = rescale(m_spread,0.0f,1.0f,m_root_pitch,72.0f);
        float xfades[16];
        for (int i=0;i<m_active_oscils;++i)
        {
            double pitch = rescale((float)i,0,m_active_oscils,m_root_pitch,m_root_pitch+(72.0f*m_spread));
            float f = rack::dsp::FREQ_C4*std::pow(1.05946309436,pitch);
            float f0;
            float f1;
            float diff;
            quantize_to_scale(f,m_scale,f0,f1,diff);
            xfades[i] = diff;
            //f = quantize_to_grid(f,m_scale,1.0);
            float detun0 = rescale((float)i,0,m_active_oscils,0.0f,f0*0.10f*m_detune);
            float detun1 = rescale((float)i,0,m_active_oscils,0.0f,f1*0.10f*m_detune);
            if (i % 2 == 1)
            {
                detun0 = -detun0;
                detun1 = -detun1;
            }
                
            f0 = clamp(f0*m_freqratio+detun0,1.0f,20000.0f);
            f1 = clamp(f1*m_freqratio+detun1,1.0f,20000.0f);
            m_osc_freqs[i*2+0] = f0;
            m_osc_freqs[i*2+1] = f1;
            
        }
        float indfloat = m_balance*4;
        int index0 = std::floor(indfloat);
        int index1 = index0+1;
        float frac = indfloat-std::floor(indfloat);
        for (int i=0;i<m_oscils.size();++i)
        {
            float bypassgain = 0.0f;
            if (i<m_active_oscils)
                bypassgain = 1.0f;
            
            float g0 = g_balance_tables[index0][i];
            float g1 = g_balance_tables[index1][i];
            float g2 = g0+(g1-g0)*frac;
            float db = rescale(g2,0.0f,1.0f,-60.0f,0.0f);
            //float db = rescale((float)i,0,m_active_oscils,gain0,gain1);
            if (db<-72.0f) db = -72.0f;
            float gain = rack::dsp::dbToAmplitude(db)*bypassgain;
            m_osc_gains[i*2+0] = gain*xfades[i];
            m_osc_gains[i*2+1] = gain; //*(1.0f-xfades[i]);
        }
    }
    std::array<float,16> fms;
    void processNextFrame(float* outbuf)
    {
        int oscilsused = 0;
        if (m_fm_mode<2)
            m_oscils[0].setFrequencies(m_osc_freqs[0],m_osc_freqs[1]);
        else
        {
            //m_oscils[m_active_oscils-1].setFrequencies(m_osc_freqs[m_active_oscils-1]);
        }
            
        int m_fm_order = 1;
        for (int i=0;i<m_oscils.size();++i)
        {
            float gain0 = m_osc_gain_smoothers[i*2+0].process(m_osc_gains[i*2+0]);
            float gain1 = m_osc_gain_smoothers[i*2+1].process(m_osc_gains[i*2+1]);
            simd::float_4 ss = m_oscils[i].processSample(0.0f);
            float s0 = ss[0];
            float s1 = ss[1];
            if (m_fm_order == 0)
                fms[i] = s0;
            s0 = reflect_value(-1.0f,s0*(1.0f+m_fold*5.0f),1.0f);
            s1 = reflect_value(-1.0f,s1*(1.0f+m_fold*5.0f),1.0f);
            if (m_fm_order == 1)
                fms[i] = s0;
            s0 *= gain0;
            s1 *= gain1;
            outbuf[i] = s0+s1;
        }
        int fm_mode = m_fm_mode;
        if (fm_mode == 0)
        {
            for (int i=1;i<m_active_oscils;++i)
            {
                float hz0 = m_osc_freqs[i*2+0];
                hz0 = hz0+(fms[0]*m_fm_amt*hz0*2.0f);
                float hz1 = m_osc_freqs[i*2+1];
                hz1 = hz1+(fms[0]*m_fm_amt*hz1*2.0f);
                m_oscils[i].setFrequencies(hz0,hz1);
            }
        } else if (fm_mode == 1)
        {
            for (int i=1;i<m_active_oscils;++i)
            {
                float hz = m_osc_freqs[i];
                m_oscils[i].setFrequency(hz+(fms[i-1]*m_fm_amt*hz));
            }
        } else if (m_fm_mode == 2)
        {
            for (int i=0;i<m_active_oscils-1;++i)
            {
                float hz = m_osc_freqs[i];
                m_oscils[i].setFrequency(hz+(fms[m_active_oscils-1]*m_fm_amt*hz*2.0f));
                //m_oscils[i].setFrequency(hz);
            }
        }
        
        
        //float scaler = m_norm_smoother.process(1.0f/(m_active_oscils*0.3f));
        //return {mix_l*scaler,mix_r*scaler};
        
    }
    int m_curScale = 0;
    void setScale(float x)
    {
        x = clamp(x,0.0f,1.0f);
        int i = x * (m_scale_bank.size()-1);
        if (i!=m_curScale)
        {
            m_curScale = i;
            m_scale = m_scale_bank[m_curScale];
        }
    }
    void setFMAmount(float a)
    {
        if (a<0.0f) a = 0.0f;
        if (a>1.0f) a = 1.0f;
        m_fm_amt = std::pow(a,2.0f);
    }
    int m_warp_mode = 0;
    void setWarp(int mode,float w)
    {
        if (w!=m_warp || mode!=m_warp_mode)
        {
            w = clamp(w,0.0f,1.0f);
            m_warp = w;
            m_warp_mode = clamp(mode,0,2);
            for (int i=0;i<m_oscils.size();++i)
                m_oscils[i].setPhaseWarp(m_warp_mode,w);
        }
    }
    void setSpread(float s)
    {
        if (s<0.0f) s = 0.0f;
        if (s>1.0f) s = 1.0f;
        m_spread = s;
        //updateOscFrequencies();
    }
    void setRootPitch(float p)
    {
        p = clamp(p,-36.0f,36.0f);
        m_root_pitch = p; 
        //updateOscFrequencies();
    }
    void setPitchOffset(float p)
    {
        p = clamp(p,-36.0f,36.0f);
        m_freqratio = std::pow(1.05946309436,p);
        //updateOscFrequencies();
    }
    void setBalance(float b)
    {
        if (b<0.0f) b = 0.0f;
        if (b>1.0f) b = 1.0f;
        m_balance = b;
        //updateOscFrequencies();
    }
    void setDetune(float d)
    {
        if (d<0.0f) d = 0.0f;
        if (d>1.0f) d = 1.0f;
        m_detune = d;
        //updateOscFrequencies();
    }
    void setFold(float f)
    {
        if (f<0.0f)
            f = 0.0f;
        if (f>1.0f)
            f = 1.0f;
        m_fold = std::pow(f,3.0f);
    }
    void setOscCount(int c)
    {
        if (c<1) c = 1;
        if (c>16) c = 16;
        m_active_oscils = c;
    }
    int getOscCount()
    {
        return m_active_oscils;
    }
    void setFMMode(int m)
    {
        if (m<0) m = 0;
        if (m>2) m = 2;
        m_fm_mode = m;
    }
    OnePoleFilter m_norm_smoother;
private:
    std::array<SIMDSimpleOsc,16> m_oscils;
    std::array<float,32> m_osc_gains;
    std::array<float,32> m_osc_freqs;
    std::array<OnePoleFilter,32> m_osc_gain_smoothers;
    
    OnePoleFilter m_balance_smoother;
    std::vector<float> m_scale;
    float m_spread = 1.0f;
    float m_root_pitch = 0.0f;
    float m_freqratio = 1.0f;
    float m_balance = 0.0f;
    float m_detune = 0.1;
    float m_fold = 0.0f;
    float m_fm_amt = 0.0f;
    int m_active_oscils = 16;
    float m_warp = 1.1f;    
    int m_fm_mode = 0;
    std::vector<std::vector<float>> m_scale_bank;
};



class XScaleOsc : public Module
{
public:
    enum OUTPUTS
    {
        OUT_AUDIO_1,
        OUT_AUDIO_2,
        OUT_LAST
    };
    enum INPUTS
    {
        IN_ROOT,
        IN_PITCH,
        IN_BALANCE,
        IN_SPREAD,
        IN_FOLD,
        IN_DETUNE,
        IN_NUM_OSCS,
        IN_FM_AMT,
        IN_WARP,
        IN_LAST
    };
    enum PARAMETERS
    {
        PAR_ROOT,
        PAR_PITCH_OFFS,
        PAR_BALANCE,
        PAR_DETUNE,
        PAR_NUM_OSCS,
        PAR_FOLD,
        PAR_SPREAD,
        PAR_WARP,
        PAR_FM_AMT,
        PAR_FM_MODE,
        PAR_SCALE,
        PAR_SCALE_BANK,
        PAR_WARP_MODE,
        PAR_NUM_OUTPUTS,
        PAR_LAST
    };
    XScaleOsc()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_BALANCE,0.0f,1.0f,0.0f,"Balance");
        configParam(PAR_ROOT,-36.0f,36.0f,0.0f,"Root");
        configParam(PAR_PITCH_OFFS,-36.0f,36.0f,0.0f,"Pitch offset");
        configParam(PAR_DETUNE,0.0f,1.0f,0.0f,"Detune");
        configParam(PAR_NUM_OSCS,1.0f,16.0f,16.0f,"Num oscillators");
        configParam(PAR_FOLD,0.0f,1.0f,0.0f,"Fold");
        configParam(PAR_SPREAD,0.0f,1.0f,0.5f,"Spread");
        configParam(PAR_WARP,0.0f,1.0f,0.5f,"Warp");
        configParam(PAR_FM_AMT,0.0f,1.0f,0.0f,"FM Amount");
        configParam(PAR_FM_MODE,0.0f,2.0f,0.0f,"FM Mode");
        configParam(PAR_SCALE,0.0f,1.0f,0.0f,"Scale");
        configParam(PAR_SCALE_BANK,0.0f,1.0f,0.0f,"Scale bank");
        configParam(PAR_WARP_MODE,0.0f,2.0f,0.0f,"Warp Mode");
        configParam(PAR_NUM_OUTPUTS,1.0f,16.0f,1.0f,"Num outputs");
        m_pardiv.setDivision(16);
        for (int i=0;i<16;++i)
        {
            float normfreq = 25.0/44100.0;
            float q = sqrt(2.0)/2.0;
            m_hpfilts[i].setParameters(rack::dsp::BiquadFilter::HIGHPASS,normfreq,q,1.0f);
        }
    }
    void process(const ProcessArgs& args) override
    {
        if (m_pardiv.process())
        {
            float bal = params[PAR_BALANCE].getValue();
            bal += inputs[IN_BALANCE].getVoltage()*0.1f;
            m_osc.setBalance(bal);
            float detune = params[PAR_DETUNE].getValue();
            detune += inputs[IN_DETUNE].getVoltage()*0.1f;
            m_osc.setDetune(detune);
            float fold = params[PAR_FOLD].getValue();
            fold += inputs[IN_FOLD].getVoltage()*0.1f;
            m_osc.setFold(fold);
            float pitch = params[PAR_PITCH_OFFS].getValue();
            pitch += inputs[IN_PITCH].getVoltage()*12.0f;
            pitch = clamp(pitch,-48.0f,48.0);
            m_osc.setPitchOffset(pitch);
            float root = params[PAR_ROOT].getValue();
            root += inputs[IN_ROOT].getVoltage()*12.0f;
            m_osc.setRootPitch(root);
            float osccount = params[PAR_NUM_OSCS].getValue();
            osccount += inputs[IN_NUM_OSCS].getVoltage() * (16.0f/10.0f);
            m_osc.setOscCount(osccount);
            float spread = params[PAR_SPREAD].getValue();
            spread += inputs[IN_SPREAD].getVoltage()*0.1f;
            m_osc.setSpread(spread);
            float warp = params[PAR_WARP].getValue();
            warp += inputs[IN_WARP].getVoltage()*0.1f;
            int wmode = params[PAR_WARP_MODE].getValue();
            m_osc.setWarp(wmode,warp);
            float fm = params[PAR_FM_AMT].getValue();
            fm += inputs[IN_FM_AMT].getVoltage()*0.1f;
            fm = clamp(fm,0.0f,1.0f);
            m_osc.setFMAmount(fm);
            int fmmode = params[PAR_FM_MODE].getValue();
            m_osc.setFMMode(fmmode);
            float scale = params[PAR_SCALE].getValue();
            m_osc.setScale(scale);
            m_osc.updateOscFrequencies();
        }
        float outs[16];
        m_osc.processNextFrame(outs);
        int numOutputs = params[PAR_NUM_OUTPUTS].getValue();
        int numOscs = m_osc.getOscCount();
        outputs[OUT_AUDIO_1].setChannels(numOutputs);
        if (outputs[OUT_AUDIO_2].isConnected())
        {
            //outputs[OUT_AUDIO_1].setVoltage(outs.first*5.0f);
            //outputs[OUT_AUDIO_2].setVoltage(outs.second*5.0f);
        }
        else
        {
            float mixed[16];
            for (int i=0;i<numOutputs;++i)
                mixed[i] = 0.0f;
            for (int i=0;i<numOscs;++i)
            {
                int outindex = i % numOutputs;
                mixed[outindex] += outs[i];
                
            }
            float normscaler = 2.0f/numOscs;
            float outgain = m_osc.m_norm_smoother.process(normscaler);
            for (int i=0;i<numOutputs;++i)
            {
                float outsample = mixed[i]*5.0f*outgain;
                outsample = m_hpfilts[i].process(outsample);
                outputs[OUT_AUDIO_1].setVoltage(outsample,i);
            }
                
            //outputs[OUT_AUDIO_1].setVoltage((outs.first+outs.second)*2.5f);
        }
    }
    ScaleOscillator m_osc;
    dsp::ClockDivider m_pardiv;
    dsp::TBiquadFilter<float> m_hpfilts[16];
};

class XScaleOscWidget : public ModuleWidget
{
public:
    XScaleOscWidget(XScaleOsc* m)
    {
        setModule(m);
        box.size.x = RACK_GRID_WIDTH * 25;
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "SCALE OSCILLATOR",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        auto port = new PortWithBackGround(m,this,XScaleOsc::OUT_AUDIO_1,1,30,"AUDIO OUT 1",true);
        new PortWithBackGround(m,this,XScaleOsc::OUT_AUDIO_2,31,30,"AUDIO OUT 2",true);
        float xc = 1.0f;
        float yc = 80.0f;
        
        addChild(new KnobInAttnWidget(this,"ROOT",XScaleOsc::PAR_ROOT,
            XScaleOsc::IN_ROOT,-1,xc,yc));
        xc+=82.0f;
        addChild(new KnobInAttnWidget(this,"BALANCE",XScaleOsc::PAR_BALANCE,
            XScaleOsc::IN_BALANCE,-1,xc,yc));
        xc+=82.0f;
        addChild(new KnobInAttnWidget(this,"PITCH",XScaleOsc::PAR_PITCH_OFFS,
            XScaleOsc::IN_PITCH,-1,xc,yc));
        xc+=82.0f;
        addChild(new KnobInAttnWidget(this,"SPREAD",XScaleOsc::PAR_SPREAD,
            XScaleOsc::IN_SPREAD,-1,xc,yc));
        xc = 1.0f;
        yc += 47.0f;
        addChild(new KnobInAttnWidget(this,"DETUNE",XScaleOsc::PAR_DETUNE,
            XScaleOsc::IN_DETUNE,-1,xc,yc));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"FOLD",XScaleOsc::PAR_FOLD,
            XScaleOsc::IN_FOLD,-1,xc,yc));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"NUM OSCS",XScaleOsc::PAR_NUM_OSCS,
            XScaleOsc::IN_NUM_OSCS,-1,xc,yc,true));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"WARP",XScaleOsc::PAR_WARP,
            XScaleOsc::IN_WARP,-1,xc,yc));
        xc = 1.0f;
        yc += 47.0f;
        addChild(new KnobInAttnWidget(this,"FM AMOUNT",XScaleOsc::PAR_FM_AMT,
            XScaleOsc::IN_FM_AMT,-1,xc,yc));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"FM MODE",XScaleOsc::PAR_FM_MODE,
            -1,-1,xc,yc,true));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"SCALE",XScaleOsc::PAR_SCALE,
            -1,-1,xc,yc));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"WARP MODE",XScaleOsc::PAR_WARP_MODE,
            -1,-1,xc,yc,true));
        xc = 1.0f;
        yc += 47.0f;
        addChild(new KnobInAttnWidget(this,"NUM OUTPUTS",XScaleOsc::PAR_NUM_OUTPUTS,
            -1,-1,xc,yc,true));
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
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelXScaleOscillator = createModel<XScaleOsc, XScaleOscWidget>("XScaleOscillator");

