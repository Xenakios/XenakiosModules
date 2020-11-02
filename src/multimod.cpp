#include "plugin.hpp"

class XLFO
{
public:
    XLFO() 
    {
        m_offsetSmoother.setAmount(0.99);
    }
    float process(float masterphase)
    {
        float smoothedOffs = m_offsetSmoother.process(m_offset);
        float out = std::sin(2*3.141592653*(smoothedOffs+masterphase));
        return out;
    }
    void setFrequency(float hz)
    {
        m_freq = hz;
    }
    void setOffset(float offs)
    {
        m_offset = offs;
    }
private:
    double m_phase = 0.0f;
    double m_freq = 1.0f;
    double m_offset = 0.0f;
    OnePoleFilter m_offsetSmoother;
};

class XMultiMod : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_RATE,
        PAR_NUMOUTPUTS,
        PAR_OFFSET,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_MODOUT,
        OUT_LAST
    };
    XMultiMod()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_RATE,0.1f,32.0f,1.0f,"Base rate");
        configParam(PAR_NUMOUTPUTS,1,16,4.0,"Number of outputs");
        configParam(PAR_OFFSET,0.0f,1.0f,0.0f,"Outputs offset");
    }
    void process(const ProcessArgs& args) override
    {
        float rate = params[PAR_RATE].getValue();
        float offset = params[PAR_OFFSET].getValue();
        int numoutputs = params[PAR_NUMOUTPUTS].getValue();
        numoutputs = clamp(numoutputs,1,16);
        outputs[OUT_MODOUT].setChannels(numoutputs);
        
        for (int i=0;i<numoutputs;++i)
        {
            //m_lfos[i].setFrequency(rate);
            m_lfos[i].setOffset(offset*(1.0/numoutputs*i));
            float out = m_lfos[i].process(m_phase);
            outputs[OUT_MODOUT].setVoltage(5.0f*out,i);
        }
        m_phase+=args.sampleTime*rate;
        if (m_phase>=1.0)
            m_phase-=1.0;
    }
private:
    XLFO m_lfos[16];
    double m_phase = 0.0;
};

class XMultiModWidget : public ModuleWidget
{
public:
    std::shared_ptr<rack::Font> m_font;
    XMultiModWidget(XMultiMod* m)
    {
        setModule(m);
        box.size.x = 120;
        addOutput(createOutput<PJ301MPort>(Vec(3, 30), m, XMultiMod::OUT_MODOUT));
        RoundBlackKnob* knob = nullptr;
        addParam(createParam<RoundBlackKnob>(Vec(3, 60), m, XMultiMod::PAR_RATE));
        addParam(createParam<RoundBlackKnob>(Vec(43, 60), m, XMultiMod::PAR_OFFSET));
        addParam(knob = createParam<RoundBlackKnob>(Vec(3, 95), m, XMultiMod::PAR_NUMOUTPUTS));
        knob->snap = true;
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

Model* modelXMultiMod = createModel<XMultiMod, XMultiModWidget>("XMultiMod");
