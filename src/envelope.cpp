#include "plugin.hpp"
#include "helperwidgets.h"
#include "jcdp_envelope.h"

const int g_ptsize = 5;

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
        
        for (int i=0;i<5;++i)
        {
            float yval = clamp(0.5+0.25*random::normal(),0.0f,1.0f);
            m_env.AddNode({1.0/4*i,yval});
        }
        m_env_len = m_env.getLastPointTime();
        configParam(PAR_RATE,-8.0f,10.0f,1.0f,"Base rate", " Hz",2,1);
        m_env_update_div.setDivision(8192);
        m_updatedPoints.reserve(65536);
    }
    void updateEnvelope(nodes_t points)
    {
        m_updatedPoints = points;
        m_doUpdate = true;
    }
    void process(const ProcessArgs& args) override
    {
        if (m_env_update_div.process())
        {
            //std::lock_guard<std::mutex> locker(m_mut);
            if (m_doUpdate)
            {
                m_env.set_all_nodes(m_updatedPoints);
                m_env.SortNodes();
                m_doUpdate = false;
            }
            
        }
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
    nodes_t m_updatedPoints;
    dsp::ClockDivider m_env_update_div;
    std::mutex m_mut;
    std::atomic<bool> m_doUpdate{false};
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
            // draw envelope line
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
            
            // draw envelope point handles
            for (int i=0;i<env.GetNumPoints();++i)
            {
                nvgBeginPath(args.vg);
                auto& pt = env.GetNodeAtIndex(i);
                float xcor = rescale(pt.pt_x,0.0f,1.0f,0.0f,box.size.x);
                float ycor = rescale(pt.pt_y,0.0f,1.0f,box.size.y,0.0f);
                if (i != m_hotPoint)
                    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xee));
                else
                    nvgFillColor(args.vg, nvgRGBA(0xff, 0x00, 0x00, 0xee));
                nvgEllipse(args.vg,xcor,ycor,g_ptsize,g_ptsize);
                nvgFill(args.vg);
            }
            // draw envelope play position
            nvgBeginPath(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xdd));
            float ppos = m_envmod->m_phase;
            float envlen = m_envmod->m_env_len;
            float xcor = rescale(ppos,0.0f,envlen,0.0f,box.size.x);
            nvgMoveTo(args.vg,xcor,0.0f);
            nvgLineTo(args.vg,xcor,box.size.y);
            nvgStroke(args.vg);
        }
        
        nvgRestore(args.vg);
    }   
    float clampPoint(breakpoint_envelope& env, int index, float input, float minval, float maxval)
    {
        float leftbound = env.getNodeLeftBound(index,0.002);
        float rightbound = env.getNodeRightBound(index,0.002);
        return clamp(input,leftbound,rightbound);
    }
    int findPoint(float xcor, float ycor)
    {
        auto& env = m_envmod->m_env;
        for (int i=0;i<env.GetNumPoints();++i)
        {
            auto& pt = env.GetNodeAtIndex(i);
            Rect r(rescale(pt.pt_x,0.0f,1.0f,0.0,box.size.x)-(g_ptsize/2.0f),
                   rescale(pt.pt_y,0.0f,1.0f,box.size.y,0.0f)-(g_ptsize/2.0f),
                    g_ptsize,g_ptsize);
                
            if (r.contains({xcor,ycor}))
            {
                return i;
            }
        }
        return -1;
    } 
    void onHover(const event::Hover& e) override 
    {
        m_hotPoint = findPoint(e.pos.x,e.pos.y);
    }
    void onButton(const event::Button& e) override
    {
        if (e.action == GLFW_RELEASE)
        {
            draggedValue_ = -1;
            return;
        }
        int index = findPoint(e.pos.x,e.pos.y);
        //auto v = qmod->quantizers[which_].getVoltages();
        if (index>=0 && !(e.mods & GLFW_MOD_SHIFT))
        {
            e.consume(this);
            draggedValue_ = index;
            initX = e.pos.x;
            initY = e.pos.y;
            return;
        }
        if (index>=0 && (e.mods & GLFW_MOD_SHIFT))
        {
            e.consume(this);
            draggedValue_ = -1;
            auto nodes = m_envmod->m_env.get_all_nodes();
            nodes.erase(nodes.begin()+index);
            m_envmod->updateEnvelope(nodes);
            return;
        }
        if (index == -1)
        {
            float newX = rescale(e.pos.x,0,box.size.x,0.0f,1.0f);
            float newY = rescale(e.pos.y,0,box.size.y,1.0f,0.0f);
            auto nodes = m_envmod->m_env.get_all_nodes();
            nodes.push_back({newX,newY});
            m_envmod->updateEnvelope(nodes);
        }
    }
    void onDragStart(const event::DragStart& e) override
    {
        dragX = APP->scene->rack->mousePos.x;
        dragY = APP->scene->rack->mousePos.y;
    }
    void onDragMove(const event::DragMove& e) override
    {
        if (draggedValue_==-1)
            return;
        auto& env = m_envmod->m_env;
        float newDragX = APP->scene->rack->mousePos.x;
        float newPosX = initX+(newDragX-dragX);
        float xp = rescale(newPosX,0.0f,box.size.x,0.0,1.0);
        xp = clampPoint(env,draggedValue_,xp,0.0f,1.0f);
        float newDragY = APP->scene->rack->mousePos.y;
        float newPosY = initY+(newDragY-dragY);
        float yp = rescale(newPosY,0.0f,box.size.y,1.0,0.0);
        yp = clamp(yp,0.0f,1.0f);
        //valX = clampValue(quant,draggedValue_,val,-5.0f,5.0f);
        //qmod->updateSingleQuantizerValue(which_,draggedValue_,val);
        //dirty = true;
        env.SetNode(draggedValue_,{xp,yp});
        //float newv = rescale(e.pos.x,0,box.size.x,-10.0f,10.0f);
    }
    
private:
    XEnvelopeModule* m_envmod = nullptr;
    float initX = 0.0f;
    float initY = 0.0f;
    float dragX = 0.0f;
    float dragY = 0.0f;
    int draggedValue_ = -1;
    int m_hotPoint = -1;
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
        port->m_text = "ENV OUT";
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
