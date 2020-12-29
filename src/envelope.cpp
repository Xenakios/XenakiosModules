#include "plugin.hpp"
#include "helperwidgets.h"
#include "jcdp_envelope.h"

class XEnvelopeModule : public rack::Module
{
public:
    enum PARAMETERS
    {
        PAR_RATE,
        PAR_ATTN_RATE,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_CV_RATE,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_ENV,
        OUT_LAST
    };
    XEnvelopeModule()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        m_env.AddNode({0.0,0.0});
        m_env.AddNode({0.1,1.0});
        m_env.AddNode({0.2,0.7});
        m_env.AddNode({2.0,0.7});
        m_env.AddNode({2.1,1.0});
        m_env.AddNode({2.5,0.0});
        m_env_len = m_env.getLastPointTime();
        configParam(PAR_RATE,-8.0f,10.0f,1.0f,"Base rate", " Hz",2,1);
    }
    void process(const ProcessArgs& args) override
    {
        float pitch = params[PAR_RATE].getValue()*12.0f;
        float rate = std::pow(2.0,1.0/12*pitch);
        float envlenscaled = m_env_len*rate;
        float output = m_env.GetInterpolatedEnvelopeValue(m_phase);
        m_phase += args.sampleTime*rate;
        
        if (m_phase>=m_env_len)
            m_phase-=m_env_len;
        if (m_out_range == 0)
            output = rescale(output,0.0f,1.0f,-5.0f,5.0f);
        else if (m_out_range == 1)
            output = rescale(output,0.0f,1.0f,0.0f,10.0f);
        outputs[OUT_ENV].setVoltage(output);
        //outputs[OUT_ENV].setVoltage(m_phase*5.0f);
    }
    double m_phase = 0.0f;
    double m_env_len = 0.0f;
    int m_out_range = 0;
    breakpoint_envelope m_env{"env"};
};

class XEnvelopeModuleWidget : public ModuleWidget
{
public:
    XEnvelopeModuleWidget(XEnvelopeModule* m)
    {
        setModule(m);
        box.size.x = 500;
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "ENVELOPE",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        PortWithBackGround<PJ301MPort>* port = nullptr;
        addOutput(port = createOutput<PortWithBackGround<PJ301MPort>>(Vec(3, 40), m, XEnvelopeModule::OUT_ENV));
        port->m_text = "AUDIO OUT";
        addChild(new KnobInAttnWidget(this,
            "RATE",XEnvelopeModule::PAR_RATE,
            XEnvelopeModule::IN_CV_RATE,XEnvelopeModule::PAR_ATTN_RATE,35,40));
    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x50, 0x50, 0x50, 0xff));
        nvgRect(args.vg,0.0f,0.0f,box.size.x,box.size.y);
        nvgFill(args.vg);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};


Model* modelXEnvelope = createModel<XEnvelopeModule, XEnvelopeModuleWidget>("XEnvelope");
