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
        /*
        m_env.AddNode({0.0,0.0});
        m_env.AddNode({0.1,1.0});
        m_env.AddNode({0.2,0.7});
        m_env.AddNode({2.0,0.7});
        m_env.AddNode({2.1,1.0});
        m_env.AddNode({2.5,0.0});
        */
        
        for (int i=0;i<16;++i)
        {
            float yval = clamp(0.5+0.25*random::normal(),0.0f,1.0f);
            m_env.AddNode({1.0/15*i,yval});
        }
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

class EnvelopeWidget : public TransparentWidget
{
public:
    EnvelopeWidget(XEnvelopeModule* m) : m_envmod(m)
    {

    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
        nvgRect(args.vg,0.0f,0.0f,box.size.x,box.size.y);
        nvgFill(args.vg);
        if (m_envmod)
        {
            nvgBeginPath(args.vg);
            
            auto& env = m_envmod->m_env;
            nvgStrokeColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0xff));
            for (int i=0;i<env.GetNumPoints();++i)
            {
                auto& pt = env.GetNodeAtIndex(i);
                float xcor = rescale(pt.pt_x,0.0f,1.0f,0.0f,box.size.x);
                float ycor = rescale(pt.pt_y,0.0f,1.0f,box.size.y,0.0f);
                
                if (i == 0)
                    nvgMoveTo(args.vg,xcor,ycor);
                else
                    nvgLineTo(args.vg,xcor,ycor);
                
                
            }
            nvgStroke(args.vg);
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xee));
            for (int i=0;i<env.GetNumPoints();++i)
            {
                auto& pt = env.GetNodeAtIndex(i);
                float xcor = rescale(pt.pt_x,0.0f,1.0f,0.0f,box.size.x);
                float ycor = rescale(pt.pt_y,0.0f,1.0f,box.size.y,0.0f);
                nvgEllipse(args.vg,xcor,ycor,3.0f,3.0f);
            }
            nvgFill(args.vg);
        }
        
        nvgRestore(args.vg);
    }    
private:
    XEnvelopeModule* m_envmod = nullptr;
};

class XEnvelopeModuleWidget : public ModuleWidget
{
public:
    EnvelopeWidget* m_envwidget = nullptr;
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
        m_envwidget = new EnvelopeWidget(m);
        addChild(m_envwidget);
        m_envwidget->box.pos = {0,90};
        m_envwidget->box.size = {500,250};
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
