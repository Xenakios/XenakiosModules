#include "plugin.hpp"
#include "helperwidgets.h"

class ScaleOscillator
{
public:
    ScaleOscillator()
    {
        for (int i=0;i<m_oscils.size();++i)
        {
            m_oscils[i].initialise([](float x){ return sin(x); },4096);
            m_oscils[i].prepare(1,44100.0f);
            m_osc_gain_smoothers[i].setAmount(0.999);
            m_osc_gains[i] = 1.0f;
        }
        m_norm_smoother.setAmount(0.999);
        double freq = 20.0;
        while (freq<20000.0)
        {
            m_scale.push_back(freq);
            freq *= 1.2;
        }
        updateOscFrequencies();
    }
    void updateOscFrequencies()
    {
        double maxfreq = rescale(m_spread,0.0f,1.0f,m_root_freq,20000.0);
        /*
        double f = m_root_freq;
        m_scale.clear();
        while (f<maxfreq)
        {
            f *= 1.5;
            m_scale.push_back(f);
        }
        */
        for (int i=0;i<m_active_oscils;++i)
        {
            double f = rescale((float)i,0,m_active_oscils,m_root_freq,maxfreq);
            f = quantize_to_grid(f,m_scale,1.0);
            float detun = rescale((float)i,0,m_active_oscils,0.0f,maxfreq*0.05*m_detune);
            if (i % 2 == 1)
                detun = -detun;
            m_oscils[i].setFrequency(f*m_freqratio+detun);
            //std::cout << i << "\t" << f << "hz\n";
        }
    }
    std::pair<float,float> getNextFrame()
    {
        float mix_l = 0.0;
        float mix_r = 0.0;
        float gain0 = 0.0f;
        float gain1 = -60.0f+60.0f*m_balance;
        int oscilsused = 0;
        for (int i=0;i<m_oscils.size();++i)
        {
            
            float db = rescale((float)i,0,m_active_oscils,gain0,gain1);
            float gain = rack::dsp::dbToAmplitude(db);
            float bypassgain = m_osc_gain_smoothers[i].process(m_osc_gains[i]);
            if (bypassgain<0.001)
                continue;
            gain = gain * bypassgain;
            ++oscilsused;
            float gain_l = 1.0f;
            float gain_r = 0.0f;
            if (i % 2 == 1)
            {
                gain_l = 0.0f;
                gain_r = 1.0f;
            }
            float s = m_oscils[i].processSample(0.0f);
            s = reflect_value(-1.0f,s*(1.0f+m_fold*5.0f),1.0f);
            
            s *= gain;
            mix_l += s * gain_l;
            mix_r += s * gain_r;
        }
        if (oscilsused>0)
        {
            float scaler = m_norm_smoother.process(1.0f/oscilsused*0.9f);
            return {mix_l*scaler,mix_r*scaler};
        }
        return {0.0f,0.0f};
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
        m_root_freq = 256.0f*std::pow(1.05946309436,p);
        //updateOscFrequencies();
    }
    void setPitchOffset(float p)
    {
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
        m_fold = f;
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
private:
    std::array<ImgWaveOscillator,16> m_oscils;
    std::array<float,16> m_osc_gains;
    std::array<OnePoleFilter,16> m_osc_gain_smoothers;
    OnePoleFilter m_norm_smoother;
    std::vector<float> m_scale;
    float m_spread = 1.0f;
    float m_root_freq = 60.0f;
    float m_freqratio = 1.0f;
    float m_balance = 0.0f;
    float m_detune = 0.1;
    float m_fold = 0.0f;
    int m_active_oscils = 16;
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
    enum PARAMETERS
    {
        PAR_ROOT,
        PAR_PITCH_OFFS,
        PAR_BALANCE,
        PAR_DETUNE,
        PAR_NUM_OSCS,
        PAR_FOLD,
        PAR_SPREAD,
        PAR_LAST
    };
    XScaleOsc()
    {
        config(PAR_LAST,0,OUT_LAST);
        configParam(PAR_BALANCE,0.0f,1.0f,0.0f,"Balance");
        configParam(PAR_ROOT,-36.0f,36.0f,-12.0f,"Root");
        configParam(PAR_PITCH_OFFS,-36.0f,36.0f,0.0f,"Pitch offset");
        configParam(PAR_DETUNE,0.0f,1.0f,0.0f,"Detune");
        configParam(PAR_NUM_OSCS,1.0f,16.0f,16.0f,"Num oscillators");
        configParam(PAR_FOLD,0.0f,1.0f,0.0f,"Fold");
        configParam(PAR_SPREAD,0.0f,1.0f,0.5f,"Spread");
        m_pardiv.setDivision(16);
    }
    void process(const ProcessArgs& args) override
    {
        if (m_pardiv.process())
        {
            m_osc.setBalance(params[PAR_BALANCE].getValue());
            m_osc.setDetune(params[PAR_DETUNE].getValue());
            m_osc.setFold(params[PAR_FOLD].getValue());
            m_osc.setPitchOffset(params[PAR_PITCH_OFFS].getValue());
            m_osc.setRootPitch(params[PAR_ROOT].getValue());
            m_osc.setOscCount(params[PAR_NUM_OSCS].getValue());
            m_osc.setSpread(params[PAR_SPREAD].getValue());
            m_osc.updateOscFrequencies();
        }
        auto outs = m_osc.getNextFrame();
        outputs[OUT_AUDIO_1].setVoltage(outs.first*5.0f);
        outputs[OUT_AUDIO_2].setVoltage(outs.second*5.0f);
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
            -1,-1,xc,yc));
        xc+=82.0f;
        addChild(new KnobInAttnWidget(this,"BALANCE",XScaleOsc::PAR_BALANCE,
            -1,-1,xc,yc));
        xc+=82.0f;
        addChild(new KnobInAttnWidget(this,"PITCH",XScaleOsc::PAR_PITCH_OFFS,
            -1,-1,xc,yc));
        xc+=82.0f;
        addChild(new KnobInAttnWidget(this,"SPREAD",XScaleOsc::PAR_SPREAD,
            -1,-1,xc,yc));
        xc = 1.0f;
        yc += 47.0f;
        addChild(new KnobInAttnWidget(this,"DETUNE",XScaleOsc::PAR_DETUNE,
            -1,-1,xc,yc));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"FOLD",XScaleOsc::PAR_FOLD,
            -1,-1,xc,yc));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"NUM OSCS",XScaleOsc::PAR_NUM_OSCS,
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
