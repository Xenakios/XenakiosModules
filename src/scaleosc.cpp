#include "plugin.hpp"
#include "helperwidgets.h"
#include "wtosc.h"
#include <array>

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
        m_phase_inc = 1.0/m_samplerate*hz;
    }
    void setPhaseWarp(int mode, float amt)
    {
        m_warp_mode = mode;
        m_warp = clamp(amt,0.0f,1.0f);
    }
    float processSample(float)
    {
        double phase_to_use;
        if (m_warp_mode == 0)
        {
            double w;
            if (m_warp<0.5)
                w = rescale(m_warp,0.0f,0.5,0.2f,1.0f);
            else
                w = rescale(m_warp,0.5f,1.0f,1.0f,4.0f);
            phase_to_use = std::pow(m_phase,w);
        } else if (m_warp_mode == 1)
        {
            double steps;
            if (m_warp<0.25)
            {
                steps = rescale(m_warp,0.00f,0.25f,128.0f,16.0f);
            } else
            {
                steps = rescale(m_warp,0.25f,1.0f,16.0f,3.0f);
                
            }
            phase_to_use = std::round(m_phase*steps)/steps;
        }
        else
        {
            double pmult = rescale(m_warp,0.0f,1.0f,0.5f,2.0f);
            phase_to_use = std::fmod(pmult*m_phase,1.0);
        }
        float r = std::sin(2*3.14159265359*phase_to_use);
        m_phase+=m_phase_inc;
        m_phase = std::fmod(m_phase,1.0);
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
        for (int i=0;i<m_oscils.size();++i)
        {
            //m_oscils[i].initialise([](float x){ return sin(x); },4096);
            m_oscils[i].prepare(1,44100.0f);
            m_osc_gain_smoothers[i].setAmount(0.999);
            m_osc_gains[i] = 1.0f;
            m_osc_freqs[i] = 440.0f;
            fms[i] = 0.0f;
        }
        m_norm_smoother.setAmount(0.999);
        m_balance_smoother.setAmount(0.999);
        std::array<float,7> ratios{81.0f/80.0f,9.0f/8.0f,1.25f,1.333333f,1.5f,9.0f/5.0f,2.0f};
        for (int i=0;i<ratios.size();++i)
        {
            double freq = 20.0;
            std::vector<float> scale;
            while (freq<40000.0)
            {
                scale.push_back(freq);
                freq *= ratios[i];
            }
            m_scale_bank.push_back(scale);
        }
        std::vector<std::string> scalafiles;
        scalafiles.push_back("/Users/teemu/Documents/Rack/plugins-v1/Xenakios/res/scala_scales/penta_opt.scl");
        scalafiles.push_back("/Users/teemu/Documents/Rack/plugins-v1/Xenakios/res/scala_scales/Ancient Greek Archytas Enharmonic.scl");
        for (auto& e : scalafiles)
        {
            auto pitches = loadScala(e,true,0.0,128);
            std::vector<float> scale;
            for (int i=0;i<pitches.size();++i)
            {
                double p = pitches[i]; //rescale(scale[i],-5.0f,5.0f,0.0f,120.0f);
                double freq = 20.0 * std::pow(1.05946309436,p);
                scale.push_back(freq);
            }
            m_scale_bank.push_back(scale);
        }
        
        m_scale.reserve(1000);
        m_scale = m_scale_bank[0];
        
        /*
        std::array<int,7> intervals{2,1,2,2,1,2,2};
        double pitch = 0;
        for (int i=0;i<256;++i)
        {
            double p = pitch;
            double f = 20.0 * std::pow(1.05946309436,p);
            if (f>20000.0)
                break;
            m_scale.push_back(f);
            pitch += intervals[i % intervals.size()];
        }
        */
        updateOscFrequencies();
    }
    void updateOscFrequencies()
    {
        //double maxfreq = rescale(m_spread,0.0f,1.0f,m_root_freq,20000.0);
        // 256.0f*std::pow(1.05946309436,p);
        
        double maxpitch = rescale(m_spread,0.0f,1.0f,m_root_pitch,72.0f);
        for (int i=0;i<m_active_oscils;++i)
        {
            double pitch = rescale((float)i,0,m_active_oscils,m_root_pitch,maxpitch);
            float f = 256.0f*std::pow(1.05946309436,pitch);
            f = quantize_to_grid(f,m_scale,1.0);
            float detun = rescale((float)i,0,m_active_oscils,0.0f,f*0.10f*m_detune);
            if (i % 2 == 1)
                detun = -detun;
            f = clamp(f*m_freqratio+detun,20.0f,20000.0f);
            // m_oscils[i].setFrequency(f);
            m_osc_freqs[i] = f;
            //std::cout << i << "\t" << f << "hz\n";
        }
    }
    std::array<float,16> fms;
    std::pair<float,float> getNextFrame()
    {
        float mix_l = 0.0;
        float mix_r = 0.0;
        float gain0 = 0.0f;
        float gain1 = -60.0f+60.0f*m_balance_smoother.process(m_balance);
        int oscilsused = 0;
        if (m_fm_mode<2)
            m_oscils[0].setFrequency(m_osc_freqs[0]);
        //else
        //    m_oscils[m_active_oscils-1].setFrequency(m_osc_freqs[m_active_oscils-1]);
        
        for (int i=0;i<m_oscils.size();++i)
        {
            
            float db = rescale((float)i,0,m_active_oscils,gain0,gain1);
            if (db<-72.0f) db = -72.0f;
            float bypassgain = m_osc_gain_smoothers[i].process(m_osc_gains[i]);
            //if (bypassgain<0.001)
            //    continue;
            float gain = rack::dsp::dbToAmplitude(db);
            gain = gain * bypassgain;
            ++oscilsused;
            float gain_l = 1.0f;
            float gain_r = 0.0f;
            if (i % 2 == 1)
            {
                gain_l = 0.0f;
                gain_r = 1.0f;
            }
            //float hz = m_osc_freqs[i];
            //m_oscils[i].setFrequency(hz+(fm*m_fm_amt*hz));
            float s = m_oscils[i].processSample(0.0f);
            
            fms[i] = s;
            
            s = reflect_value(-1.0f,s*(1.0f+m_fold*5.0f),1.0f);
            
            s *= gain;
            mix_l += s * gain_l;
            mix_r += s * gain_r;
        }
        int fm_mode = m_fm_mode;
        if (fm_mode == 0)
        {
            for (int i=1;i<m_active_oscils;++i)
            {
                float hz = m_osc_freqs[i];
                m_oscils[i].setFrequency(hz+(fms[0]*m_fm_amt*hz));
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
            for (int i=0;i<m_active_oscils;++i)
            {
                float hz = m_osc_freqs[i];
                m_oscils[i].setFrequency(hz+(fms[m_active_oscils-1]*m_fm_amt*hz));
                //m_oscils[i].setFrequency(hz);
            }
        }
        
        
        float scaler = m_norm_smoother.process(1.0f/(m_active_oscils*0.3f));
        return {mix_l*scaler,mix_r*scaler};
        
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
        if (c == m_active_oscils)
            return;
        if (c<1) c = 1;
        if (c>16) c = 16;
        for (int i=0;i<16;++i)
        {
            if (i<c)
                m_osc_gains[i] = 1.0f;
            else m_osc_gains[i] = 0.0f;
        }
        m_active_oscils = c;
    }
    void setFMMode(int m)
    {
        if (m<0) m = 0;
        if (m>2) m = 2;
        m_fm_mode = m;
    }
private:
    std::array<SimpleOsc,16> m_oscils;
    std::array<float,16> m_osc_gains;
    std::array<float,16> m_osc_freqs;
    std::array<OnePoleFilter,16> m_osc_gain_smoothers;
    OnePoleFilter m_norm_smoother;
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
    float m_warp = 0.0f;    
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
        PAR_LAST
    };
    XScaleOsc()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_BALANCE,0.0f,1.0f,0.0f,"Balance");
        configParam(PAR_ROOT,-36.0f,36.0f,-12.0f,"Root");
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
        m_pardiv.setDivision(16);
    }
    void process(const ProcessArgs& args) override
    {
        if (m_pardiv.process())
        {
            float bal = params[PAR_BALANCE].getValue();
            bal += inputs[IN_BALANCE].getVoltage()*0.1f;
            m_osc.setBalance(bal);
            float detune = params[PAR_DETUNE].getValue();
            detune += inputs[IN_DETUNE].getVoltage();
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
        auto outs = m_osc.getNextFrame();
        if (outputs[OUT_AUDIO_2].isConnected())
        {
            outputs[OUT_AUDIO_1].setVoltage(outs.first*5.0f);
            outputs[OUT_AUDIO_2].setVoltage(outs.second*5.0f);
        }
        else
        {
            outputs[OUT_AUDIO_1].setVoltage((outs.first+outs.second)*2.5f);
        }
    }
    ScaleOscillator m_osc;
    dsp::ClockDivider m_pardiv;
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

