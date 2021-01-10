#include "plugin.hpp"
#include "helperwidgets.h"
#include "jcdp_envelope.h"
#include <atomic>
#include <mutex>

const int g_ptsize = 5;

class XEnvelopeModule : public rack::Module
{
public:
    enum PARAMETERS
    {
        PAR_RATE,
        PAR_ATTN_RATE,
        PAR_PLAYMODE,
        PAR_ACTIVE_ENVELOPE,
        PAR_ATTN_ACTENV,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_CV_RATE,
        IN_TRIGGER,
        IN_POSITION,
        IN_ACTENV,
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
        for (int i=0;i<16;++i)
        {
            std::unique_ptr<breakpoint_envelope> env(new breakpoint_envelope("envelope"+std::to_string(i+1)));
            env->AddNode({0.0,0.5});
            env->AddNode({1.0,0.5});
            m_envelopes.push_back(std::move(env));
        }
        
        m_env_len = 1.0f;
        configParam(PAR_RATE,-8.0f,10.0f,1.0f,"Base rate", " Hz",2,1);
        configParam(PAR_ATTN_RATE,-1.0f,1.0f,0.0f,"Base rate CV level");
        configParam(PAR_PLAYMODE,0.0f,1.0f,0.0f,"Play mode");
        configParam(PAR_ACTIVE_ENVELOPE,0.0f,15.0f,0.0f,"Envelope select");
        configParam(PAR_ATTN_RATE,-1.0f,1.0f,0.0f,"Envelope select CV level");
        m_env_update_div.setDivision(8192);
        m_updatedPoints.reserve(65536);
    }
    void updateEnvelope(nodes_t points)
    {
        m_updatedPoints = points;
        m_doUpdate = true;
    }
    json_t* dataToJson() override
    {
        json_t* resultJ = json_object();
        json_t* arrayJ = json_array();
        
        for (int i=0;i<m_envelopes[0]->GetNumPoints();++i)
        {
            json_t* ptJ = json_object();
            auto& pt = m_envelopes[0]->GetNodeAtIndex(i);
            json_object_set(ptJ,"x",json_real(pt.pt_x));
            json_object_set(ptJ,"y",json_real(pt.pt_y));
            json_array_append(arrayJ,ptJ);
        }
        
        json_object_set(resultJ,"envelope_v1",arrayJ);
        json_object_set(resultJ,"outputrange",json_integer(m_out_range));
        return resultJ;
    }
    void dataFromJson(json_t* root) override
    {
        json_t* arrayJ = json_object_get(root,"envelope_v1");
        json_t* outrngJ = json_object_get(root,"outputrange");
        if (outrngJ)
            m_out_range = json_integer_value(outrngJ);
        if (arrayJ)
        {
            int numpoints = json_array_size(arrayJ);
            nodes_t points;
            for (int i=0;i<numpoints;++i)
            {
                json_t* ptJ = json_array_get(arrayJ,i);
                if (ptJ)
                {
                    json_t* ptxj = json_object_get(ptJ,"x");
                    json_t* ptyj = json_object_get(ptJ,"y");
                    float ptx = json_number_value(ptxj);
                    float pty = json_number_value(ptyj);
                    points.push_back({ptx,pty});
                }
            }
            if (points.size()>0)
            {
                updateEnvelope(points);
            }
        }
    }
    void process(const ProcessArgs& args) override
    {
        int actenv = params[PAR_ACTIVE_ENVELOPE].getValue();
        if (m_env_update_div.process())
        {
            //std::lock_guard<std::mutex> locker(m_mut);
            if (m_doUpdate)
            {
                m_envelopes[actenv]->set_all_nodes(m_updatedPoints);
                m_envelopes[actenv]->SortNodes();
                m_doUpdate = false;
            }
            
        }
        float pitch = params[PAR_RATE].getValue()*12.0f;
        pitch += inputs[IN_CV_RATE].getVoltage()*params[PAR_ATTN_RATE].getValue()*12.0f;
        float rate = std::pow(2.0,1.0/12*pitch);
        float envlenscaled = m_env_len*rate;
        double phasetouse = m_phase;
        if (inputs[IN_POSITION].isConnected())
            phasetouse = rescale(inputs[IN_POSITION].getVoltage(),-5.0f,5.0f,0.0f,1.0f);
        m_phase_used = phasetouse;
        float output = m_envelopes[actenv]->GetInterpolatedEnvelopeValue(phasetouse);
        m_phase += args.sampleTime*rate;
        int playmode = params[PAR_PLAYMODE].getValue();
        if (playmode == 1)
        {
            if (m_phase>=m_env_len)
                m_phase-=m_env_len;
        } 
        if (resetTrigger.process(inputs[IN_TRIGGER].getVoltage()))
            m_phase = 0.0f;
        m_last_value = output;
        if (m_out_range == 0)
            output = rescale(output,0.0f,1.0f,-5.0f,5.0f);
        else if (m_out_range == 1)
            output = rescale(output,0.0f,1.0f,0.0f,10.0f);
        outputs[OUT_ENV].setVoltage(output);
        //outputs[OUT_ENV].setVoltage(m_phase*5.0f);
    }
    double m_phase = 0.0;
    double m_phase_used = 0.0;
    double m_env_len = 0.0f;
    int m_out_range = 0;
    float m_last_value = 0.0f;
    
    nodes_t m_updatedPoints;
    dsp::ClockDivider m_env_update_div;
    std::mutex m_mut;
    std::atomic<bool> m_doUpdate{false};
    rack::dsp::SchmittTrigger resetTrigger;
    breakpoint_envelope& getActiveEnvelope()
    {
        return *m_envelopes[(int)params[PAR_ACTIVE_ENVELOPE].getValue()];
    }
private:
    std::vector<std::unique_ptr<breakpoint_envelope>> m_envelopes;
};

struct OutputRangeItem : MenuItem
{
    XEnvelopeModule* em = nullptr;
    OutputRangeItem(XEnvelopeModule* m) : em(m) {}
    Menu *createChildMenu() override 
    {
        Menu *submenu = new Menu();
        auto item = createMenuItem([this](){ em->m_out_range = 0; },"-5 to 5 volts");
        submenu->addChild(item);
        item = createMenuItem([this](){ em->m_out_range = 1; },"0 to 10 volts");
        submenu->addChild(item);
        
        return submenu;
    }

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
            
            auto& env = m_envmod->getActiveEnvelope();
            nvgStrokeColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0xff));
            int numpts = env.GetNumPoints();
            for (int i=0;i<numpts;++i)
            {
                auto& pt = env.GetNodeAtIndex(i);
                float xcor = rescale(pt.pt_x,0.0f,1.0f,0.0f,box.size.x);
                float ycor = rescale(pt.pt_y,0.0f,1.0f,box.size.y,0.0f);
                
                if (i == 0)
                {
                    if (xcor>0.0f)
                    {
                        nvgMoveTo(args.vg,0.0f,ycor);
                        nvgLineTo(args.vg,xcor,ycor);    
                    }
                        
                    else
                        nvgMoveTo(args.vg,xcor,ycor);
                }
                else
                {
                    nvgLineTo(args.vg,xcor,ycor);
                }
                    
                if (i == numpts-1 && xcor<box.size.x)
                {
                    nvgLineTo(args.vg,box.size.x,ycor);
                }
                
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
            /*
            nvgBeginPath(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xdd));
            float ppos = clamp(m_envmod->m_phase_used,0.0f,1.0f);
            float envlen = m_envmod->m_env_len;
            float xcor = rescale(ppos,0.0f,envlen,0.0f,box.size.x);
            nvgMoveTo(args.vg,xcor,0.0f);
            nvgLineTo(args.vg,xcor,box.size.y);
            nvgStroke(args.vg);
            */
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xee));
            float ppos = clamp(m_envmod->m_phase_used,0.0f,1.0f);
            float envlen = m_envmod->m_env_len;
            float xcor = rescale(ppos,0.0f,envlen,0.0f,box.size.x);
            float pval = m_envmod->m_last_value;
            float ycor = rescale(pval,0.0f,1.0f,box.size.y,0.0f);
            nvgEllipse(args.vg,xcor,ycor,2.5f,2.5f);
            nvgFill(args.vg);
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
        auto& env = m_envmod->getActiveEnvelope();
        for (int i=0;i<env.GetNumPoints();++i)
        {
            auto& pt = env.GetNodeAtIndex(i);
            Rect r(rescale(pt.pt_x,0.0f,1.0f,0.0,box.size.x)-(g_ptsize),
                   rescale(pt.pt_y,0.0f,1.0f,box.size.y,0.0f)-(g_ptsize),
                    g_ptsize*2,g_ptsize*2);
                
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
        auto& env = m_envmod->getActiveEnvelope();
        if (index>=0 && (e.mods & GLFW_MOD_SHIFT))
        {
            
            if (env.GetNumPoints()>1)
            {
                e.consume(this);
                draggedValue_ = -1;
                auto nodes = env.get_all_nodes();
                nodes.erase(nodes.begin()+index);
                m_envmod->updateEnvelope(nodes);
            }
            
            return;
        }
        if (index == -1)
        {
            float newX = rescale(e.pos.x,0,box.size.x,0.0f,1.0f);
            float newY = rescale(e.pos.y,0,box.size.y,1.0f,0.0f);
            auto nodes = env.get_all_nodes();
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
        auto& env = m_envmod->getActiveEnvelope();
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
    XEnvelopeModule* m_emod = nullptr;
    XEnvelopeModuleWidget(XEnvelopeModule* m)
    {
        setModule(m);
        m_emod = m;
        box.size.x = 506.5;
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "ENVELOPE",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        PortWithBackGround<PJ301MPort>* port = nullptr;
        addOutput(port = createOutput<PortWithBackGround<PJ301MPort>>(Vec(5, 40), m, XEnvelopeModule::OUT_ENV));
        port->m_text = "ENV OUT";
        addInput(port = createInput<PortWithBackGround<PJ301MPort>>(Vec(35, 40), m, XEnvelopeModule::IN_TRIGGER));
        port->m_text = "RST";
        port->m_is_out = false;
        addInput(port = createInput<PortWithBackGround<PJ301MPort>>(Vec(65, 40), m, XEnvelopeModule::IN_POSITION));
        port->m_text = "POS";
        port->m_is_out = false;
        addChild(new KnobInAttnWidget(this,
            "RATE",XEnvelopeModule::PAR_RATE,
            XEnvelopeModule::IN_CV_RATE,XEnvelopeModule::PAR_ATTN_RATE,2,70));
        addChild(new KnobInAttnWidget(this,
            "PLAY MODE",XEnvelopeModule::PAR_PLAYMODE,
            -1,-1,84,70,true));
        addChild(new KnobInAttnWidget(this,
            "ENVELOPE SEL",XEnvelopeModule::PAR_ACTIVE_ENVELOPE,
            XEnvelopeModule::IN_ACTENV,XEnvelopeModule::PAR_ATTN_ACTENV,166,70,true));
        m_envwidget = new EnvelopeWidget(m);
        addChild(m_envwidget);
        m_envwidget->box.pos = {0,120};
        m_envwidget->box.size = {500,250};
    }
    void appendContextMenu(Menu *menu) override 
    {
        menu->addChild(new MenuEntry);
        OutputRangeItem* it = new OutputRangeItem(m_emod);
        it->em = m_emod;
        it->text = "Output range";
        menu->addChild(it);
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
