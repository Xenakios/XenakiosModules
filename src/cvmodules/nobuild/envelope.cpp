#include "../plugin.hpp"
#include "../helperwidgets.h"
#include "../jcdp_envelope.h"
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
        PAR_SEL_ENV_PLAYBACK,
        PAR_ATTN_ACTENV,
        PAR_NUM_OUTPUTS,
        PAR_SEL_ENV_EDIT,
        ENUMS(PAR_ENVSOURCE, 16),
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
        OUT_EOC,
        OUT_LAST
    };
    XEnvelopeModule()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        for (int i=0;i<16;++i)
        {
            std::unique_ptr<breakpoint_envelope> env(new breakpoint_envelope("envelope"+std::to_string(i+1)));
            env->AddNode({0.0,0.5,2});
            env->AddNode({1.0,0.5,2});
            m_envelopes.push_back(std::move(env));
            currentEnvPoints[i]=-1;
            m_last_values[i] = 0.0f;
        }
        
        m_env_len = 1.0f;
        configParam(PAR_RATE,-8.0f,10.0f,1.0f,"Base rate", " Hz",2,1);
        configParam(PAR_ATTN_RATE,-1.0f,1.0f,0.0f,"Base rate CV level");
        configParam(PAR_PLAYMODE,0.0f,1.0f,0.0f,"Play mode");
        configParam(PAR_SEL_ENV_PLAYBACK,0.0f,15.0f,0.0f,"Envelope select");
        configParam(PAR_ATTN_ACTENV,-1.0f,1.0f,0.0f,"Envelope select CV level");
        configParam(PAR_NUM_OUTPUTS,1.0f,16.0f,1.0f,"Number of outputs");
        configParam(PAR_SEL_ENV_EDIT,0.0f,15.0f,0.0f,"Envelope edit select");
        for (int i=0;i<16;++i)
        {
            configParam(PAR_ENVSOURCE+i,-1.0f,15.0f,0.0f,"Output "+std::to_string(i+1)+" source envelope");
        }
        m_env_update_div.setDivision(8192);
        m_updatedPoints.reserve(65536);
        
    }
    void updateEnvelope(nodes_t points)
    {
        //m_updatedPoints = points;
        //m_doUpdate = true;
        m_mut.lock();
        auto& env = getEditEnvelope();
        env.set_all_nodes(points);
        env.SortNodes();
        m_mut.unlock();
    }
    json_t* dataToJson() override
    {
        json_t* resultJ = json_object();
        json_t* envelopesArrayJ = json_array();
        for (int j=0;j<m_envelopes.size();++j)
        {
            json_t* pointsarrayJ = json_array();
        
            for (int i=0;i<m_envelopes[j]->GetNumPoints();++i)
            {
                json_t* ptJ = json_object();
                auto& pt = m_envelopes[j]->GetNodeAtIndex(i);
                json_object_set(ptJ,"x",json_real(pt.pt_x));
                json_object_set(ptJ,"y",json_real(pt.pt_y));
                json_object_set(ptJ,"sh",json_integer(pt.Shape));
                json_array_append(pointsarrayJ,ptJ);
            }
            json_array_append(envelopesArrayJ,pointsarrayJ);
        }
        
        json_object_set(resultJ,"envelopes_v1",envelopesArrayJ);
        json_object_set(resultJ,"outputrange",json_integer(m_out_range));
        return resultJ;
    }
    void dataFromJson(json_t* root) override
    {
        json_t* envelopesarrayJ = json_object_get(root,"envelopes_v1");
        json_t* outrngJ = json_object_get(root,"outputrange");
        if (outrngJ)
            m_out_range = json_integer_value(outrngJ);
        if (envelopesarrayJ)
        {
            int numEnvelopes = json_array_size(envelopesarrayJ);
            for (int i=0;i<numEnvelopes;++i)
            {
                json_t* pointsArrayJ = json_array_get(envelopesarrayJ,i);
                int numpoints = json_array_size(pointsArrayJ);
                nodes_t points;
                for (int j=0;j<numpoints;++j)
                {
                    json_t* ptJ = json_array_get(pointsArrayJ,j);
                    if (ptJ)
                    {
                        json_t* ptxj = json_object_get(ptJ,"x");
                        json_t* ptyj = json_object_get(ptJ,"y");
                        json_t* ptshj = json_object_get(ptJ,"sh");
                        float ptx = json_number_value(ptxj);
                        float pty = json_number_value(ptyj);
                        int ptsh = json_integer_value(ptshj);
                        points.push_back({ptx,pty,ptsh});
                    }
                }
                if (points.size()>0)
                {
                    // OK, this *looks* a bit nasty, but might not have that much impact after all...
                    m_mut.lock();
                    m_envelopes[i]->set_all_nodes(points);
                    m_mut.unlock();
                }
            }
            
            
        }
    }
    int currentEnvPoints[16];
    int m_numOuts = 1;
    void process(const ProcessArgs& args) override
    {
        // OK, this locking scheme *looks* a bit nasty, but might not have that much impact after all...
        if (m_mut.try_lock()==false)
            return;
        else
        {
        float actenvf = params[PAR_SEL_ENV_PLAYBACK].getValue();
        int update_env = actenvf;
        actenvf += inputs[IN_ACTENV].getVoltage()*params[PAR_ATTN_ACTENV].getValue()*3.2f;
        actenvf = clamp(actenvf,0.0f,15.0f);
        int actenv = actenvf;
        if (m_env_update_div.process())
        {
            int maxout = -1;
            for (int i=0;i<16;++i)
            {
                int srcindex = params[PAR_ENVSOURCE+i].getValue();
                if (srcindex>=0)
                    maxout = i;
            }
            if (maxout<0)
                maxout = 0;
            m_numOuts = maxout+1;
            outputs[OUT_ENV].setChannels(m_numOuts);
        }
        float pitch = params[PAR_RATE].getValue()*12.0f;
        pitch += inputs[IN_CV_RATE].getVoltage()*params[PAR_ATTN_RATE].getValue()*12.0f;
        float rate = std::pow(2.0,1.0/12*pitch);
        float envlenscaled = m_env_len*rate;
        double phasetouse = m_phase;
        if (inputs[IN_POSITION].isConnected())
            phasetouse = rescale(inputs[IN_POSITION].getVoltage(),-5.0f,5.0f,0.0f,1.0f);
        m_phase_used = phasetouse;
        int numouts = m_numOuts;
        //int numouts = params[PAR_NUM_OUTPUTS].getValue();
        //if (outputs[OUT_ENV].getChannels()!=numouts)
        //    outputs[OUT_ENV].setChannels(numouts);
        
        /*
        for (int i=0;i<numouts;++i)
        {
            int envindex = (actenv+i) & 15;
            float output = m_envelopes[envindex]->GetInterpolatedEnvelopeValue(phasetouse,&currentEnvPoints[envindex]);
            output += m_global_rand_offset;
            output = clamp(output,0.0f,1.0f);
            m_last_values[envindex] = output;
            if (m_out_range == 0)
                output = rescale(output,0.0f,1.0f,-5.0f,5.0f);
            else if (m_out_range == 1)
                output = rescale(output,0.0f,1.0f,0.0f,10.0f);
            outputs[OUT_ENV].setVoltage(output,i);
        }
        */
        
        for (int i=0;i<m_numOuts;++i)
        {
            int envindex = (actenv+i) & 15;
            int srcindex = params[PAR_ENVSOURCE+i].getValue();
            if (srcindex>=0)
            {
                float output = m_envelopes[srcindex]->GetInterpolatedEnvelopeValue(phasetouse,&currentEnvPoints[envindex]);
                output += m_global_rand_offset;
                output = clamp(output,0.0f,1.0f);
                m_last_values[envindex] = output;
                if (m_out_range == 0)
                    output = rescale(output,0.0f,1.0f,-5.0f,5.0f);
                else if (m_out_range == 1)
                    output = rescale(output,0.0f,1.0f,0.0f,10.0f);
                
                outputs[OUT_ENV].setVoltage(output,i);
            }
            
        }
        
        m_phase += args.sampleTime*rate;
        int playmode = params[PAR_PLAYMODE].getValue();
        bool cycle = false;
        if (playmode == 1)
        {
            if (m_phase>=m_env_len)
            {
                m_phase-=m_env_len;
                cycle = true;
                updateGlobalRandomOffset();
            }
                
        } else
        {
            if (m_phase>=m_env_len)
            {
                cycle = true;
                updateGlobalRandomOffset();
            }
        }
        if (cycle)
        {
            eocPulse.trigger();
        }
        if (outputs[OUT_EOC].isConnected())
        {
            if (eocPulse.process(args.sampleTime))
                outputs[OUT_EOC].setVoltage(10.0f);
            else
                outputs[OUT_EOC].setVoltage(0.0f);
        }
        if (resetTrigger.process(inputs[IN_TRIGGER].getVoltage()))
        {
            m_phase = 0.0f;
            updateGlobalRandomOffset();
        }
        
        
        
        m_mut.unlock();
        }
    }
    void updateGlobalRandomOffset()
    {
        float delta = random::normal()*0.05f;
        m_global_rand_offset = delta;
    }
    double m_phase = 0.0;
    double m_phase_used = 0.0;
    double m_env_len = 0.0f;
    float m_global_rand_offset = 0.0f;
    int m_out_range = 0;
    float m_last_values[16];
    
    nodes_t m_updatedPoints;
    dsp::ClockDivider m_env_update_div;
    spinlock m_mut;
    std::atomic<bool> m_doUpdate{false};
    rack::dsp::SchmittTrigger resetTrigger;
    breakpoint_envelope& getActiveEnvelope()
    {
        return *m_envelopes[(int)params[PAR_SEL_ENV_PLAYBACK].getValue()];
    }
    breakpoint_envelope& getEditEnvelope()
    {
        return *m_envelopes[(int)params[PAR_SEL_ENV_EDIT].getValue()];
    }
    int getPlayingPoint()
    {
        int index = params[PAR_SEL_ENV_PLAYBACK].getValue();
        return currentEnvPoints[index];
    }
    float getPlayValue()
    {
        int index = params[PAR_SEL_ENV_EDIT].getValue();
        return m_last_values[index];
    }
    bool editAndPlayMatch()
    {
        int a = params[PAR_SEL_ENV_PLAYBACK].getValue();
        int b = params[PAR_SEL_ENV_EDIT].getValue();
        return a==b;
    }
    rack::dsp::PulseGenerator eocPulse;
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
            
            auto& env = m_envmod->getEditEnvelope();
            nvgStrokeColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0xff));
            int numpts = env.GetNumPoints();
            /*
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
            */
            int envw = box.size.x;
            for (int i=0;i<envw;i+=2)
            {
                float xcor = i;
                float normtime = rescale((float)i,0.0f,envw,m_horiz_start,m_horiz_end);
                float envval = env.GetInterpolatedEnvelopeValue(normtime);
                float ycor = rescale(envval,0.0f,1.0f,box.size.y,0.0f);
                if (i == 0)
                {
                    nvgMoveTo(args.vg,xcor,ycor);   
                } else
                {
                    nvgLineTo(args.vg,xcor,ycor);
                }
            }
            nvgStroke(args.vg);
            // draw envelope point handles
            for (int i=0;i<env.GetNumPoints();++i)
            {
                
                auto& pt = env.GetNodeAtIndex(i);
                float xcor = rescale(pt.pt_x,m_horiz_start,m_horiz_end,0.0f,box.size.x);
                float ycor = rescale(pt.pt_y,0.0f,1.0f,box.size.y,0.0f);
                //if (xcor>=0.0 && xcor<box.size.x)
                {
                    nvgBeginPath(args.vg);
                    if (i != m_hotPoint)
                        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xee));
                    else
                        nvgFillColor(args.vg, nvgRGBA(0xff, 0x00, 0x00, 0xee));
                    nvgEllipse(args.vg,xcor,ycor,g_ptsize,g_ptsize);
                    nvgFill(args.vg);
                }
                
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
            //if (m_envmod->editAndPlayMatch())
            {
                nvgBeginPath(args.vg);
                nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xee));
                float ppos = clamp(m_envmod->m_phase_used,0.0f,1.0f);
                float envlen = m_envmod->m_env_len;
                float xcor = rescale(ppos,envlen*m_horiz_start,envlen*m_horiz_end,0.0f,box.size.x);
                float pval = m_envmod->getPlayValue();
                float ycor = rescale(pval,0.0f,1.0f,box.size.y,0.0f);
                nvgEllipse(args.vg,xcor,ycor,2.5f,2.5f);
                nvgFill(args.vg);    
            }
            
            // debug texts
            nvgFontSize(args.vg, 15);
            nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            char buf[200];
            int ptindex = m_envmod->getPlayingPoint();
            sprintf(buf,"playing point %d num outputs %d",ptindex,m_envmod->m_numOuts);
            nvgText(args.vg, 3 , 10, buf, NULL);
        }
        
        nvgRestore(args.vg);
    }   
    float clampPoint(breakpoint_envelope& env, int index, float input, float minval, float maxval)
    {
        float leftbound = env.getNodeLeftBound(index,0.002);
        float rightbound = env.getNodeRightBound(index,0.002);
        return clamp(input,leftbound,rightbound);
    }
    float snapPointTime(breakpoint_envelope& env, int index, float xpos)
    {
        float quantized = std::round(xpos*8)/8.0;
        if (std::fabs(quantized-xpos)<0.01)
            return quantized;
        return xpos;
    }
    float snapPointValue(breakpoint_envelope& env, int index, float ypos)
    {
        float quantized = std::round(ypos*10)/10.0f;
        if (std::fabs(quantized-ypos)<0.01)
            return quantized;
        return ypos;
    }
    int findPoint(float xcor, float ycor)
    {
        auto& env = m_envmod->getEditEnvelope();
        for (int i=0;i<env.GetNumPoints();++i)
        {
            auto& pt = env.GetNodeAtIndex(i);
            Rect r(rescale(pt.pt_x,m_horiz_start,m_horiz_end,0.0,box.size.x)-(g_ptsize),
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
    bool rightClickInProgress = false;
    int shapePointIndex = -1;
    void onButton(const event::Button& e) override
    {
        shapePointIndex = -1;
        if (e.action == GLFW_RELEASE) // || rightClickInProgress)
        {
            draggedValue_ = -1;
            rightClickInProgress = false;
            return;
        }
        auto& env = m_envmod->getEditEnvelope();
        int index = findPoint(e.pos.x,e.pos.y);
        
        if (index>=0 && !(e.mods & GLFW_MOD_SHIFT) && e.button == GLFW_MOUSE_BUTTON_LEFT)
        {
            e.consume(this);
            draggedValue_ = index;
            initX = e.pos.x;
            initY = e.pos.y;
            return;
        }
        if (index>=0 && !(e.mods & GLFW_MOD_SHIFT) && e.button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            shapePointIndex = index;
            ui::Menu *menu = createMenu();
            MenuLabel *mastSetLabel = new MenuLabel();
			mastSetLabel->text = "Set point shape";
			menu->addChild(mastSetLabel);
            auto item = createMenuItem([this,index](){ setPointShape(index,0); },"Power 1");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,1); },"Power 2");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,4); },"Power 3");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,2); },"Linear");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,5); },"Random 1");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,6); },"Random 2");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,9); },"Random 3");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,3); },"Sine 1");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,7); },"Quantized 1");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,8); },"Quantized 2");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,10); },"ZigZag 1");
            menu->addChild(item);
            item = createMenuItem([this,index](){ setPointShape(index,11); },"ZigZag 2");
            menu->addChild(item);
            e.consume(this);
            rightClickInProgress = true;
            draggedValue_ = -1;
            return;
        }
        if (index>=0 && (e.mods & GLFW_MOD_SHIFT) && e.button == GLFW_MOUSE_BUTTON_LEFT)
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
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & GLFW_MOD_CONTROL))
        {
            m_envmod->m_phase = rescale(e.pos.x,0.0f,box.size.x,0.0f,1.0f);
            e.consume(this);
            return;
        }
        if (index == -1 && e.button == GLFW_MOUSE_BUTTON_LEFT)
        {
            float newX = rescale(e.pos.x,0,box.size.x,m_horiz_start,m_horiz_end);
            float newY = rescale(e.pos.y,0,box.size.y,1.0f,0.0f);
            auto nodes = env.get_all_nodes();
            nodes.push_back({newX,newY,2});
            m_envmod->updateEnvelope(nodes);
            return;
        }
        
    }
    void setPointShape(int index, int sh)
    {
        auto& env = m_envmod->getEditEnvelope();
        env.SetNodeShape(index,sh);
        //auto& pt = env.GetNodeAtIndex(index);
        //pt.Shape = sh;
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
        auto& env = m_envmod->getEditEnvelope();
        float newDragX = APP->scene->rack->mousePos.x;
        float newPosX = initX+(newDragX-dragX);
        float xp = rescale(newPosX,0.0f,box.size.x,m_horiz_start,m_horiz_end);
        xp = clampPoint(env,draggedValue_,xp,0.0f,1.0f);
        xp = snapPointTime(env,draggedValue_,xp);
        float newDragY = APP->scene->rack->mousePos.y;
        float newPosY = initY+(newDragY-dragY);
        float yp = rescale(newPosY,0.0f,box.size.y,1.0,0.0);
        yp = clamp(yp,0.0f,1.0f);
        yp = snapPointValue(env,0,yp);
        //valX = clampValue(quant,draggedValue_,val,-5.0f,5.0f);
        //qmod->updateSingleQuantizerValue(which_,draggedValue_,val);
        //dirty = true;
        int ptsh = env.GetNodeAtIndex(draggedValue_).Shape;
        env.SetNode(draggedValue_,{xp,yp,ptsh});
        //float newv = rescale(e.pos.x,0,box.size.x,-10.0f,10.0f);
    }
    void setHorizontalViewRange(float x0, float x1)
    {
        m_horiz_start = x0;
        m_horiz_end = x1;
    }
private:
    XEnvelopeModule* m_envmod = nullptr;
    float initX = 0.0f;
    float initY = 0.0f;
    float dragX = 0.0f;
    float dragY = 0.0f;
    int draggedValue_ = -1;
    int m_hotPoint = -1;
    float m_horiz_start = 0.0f;
    float m_horiz_end = 1.0f;
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
        box.size.x = 40 * 15;
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "ENVELOPE",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        new PortWithBackGround(m,this,XEnvelopeModule::OUT_ENV,5,10,"ENV OUT",true);
        new PortWithBackGround(m,this,XEnvelopeModule::IN_TRIGGER,35,10,"RST",false);
        new PortWithBackGround(m,this,XEnvelopeModule::IN_POSITION,65,10,"POS",false);
        new PortWithBackGround(m,this,XEnvelopeModule::OUT_EOC,95,10,"EOC",true);
        addChild(new KnobInAttnWidget(this,
            "RATE",XEnvelopeModule::PAR_RATE,
            XEnvelopeModule::IN_CV_RATE,XEnvelopeModule::PAR_ATTN_RATE,2.0f,70.0f));
        addChild(new KnobInAttnWidget(this,
            "PLAY MODE",XEnvelopeModule::PAR_PLAYMODE,
            -1,-1,84.0f,70.0f,true));
        addChild(new KnobInAttnWidget(this,
            "ENVELOPE SEL",XEnvelopeModule::PAR_SEL_ENV_PLAYBACK,
            XEnvelopeModule::IN_ACTENV,XEnvelopeModule::PAR_ATTN_ACTENV,166.0f,70.0f,true));
        addChild(new KnobInAttnWidget(this,
            "NUM OUTS",XEnvelopeModule::PAR_NUM_OUTPUTS,
            -1,-1,248.0f,70.0f,true));
        addChild(new KnobInAttnWidget(this,
            "EDIT ENVELOPE",XEnvelopeModule::PAR_SEL_ENV_EDIT,
            -1,-1,248.0f+82,70.0f,true));
        // 120
        for (int i=0;i<16;++i)
        {
            RoundSmallBlackKnob* pot;
            addParam(pot = createParam<RoundSmallBlackKnob>(Vec(1+30*i, 120), module, XEnvelopeModule::PAR_ENVSOURCE+i));
            pot->snap = true;
        }
        m_envwidget = new EnvelopeWidget(m);
        addChild(m_envwidget);
        m_envwidget->box.pos = {1,150};
        m_envwidget->box.size = {598,217};
        ZoomScrollWidget* zsw = new ZoomScrollWidget;
        addChild(zsw);
        zsw->box.pos = {1,367};
        zsw->box.size = {598,10};
        zsw->OnRangeChange=[this](float t0, float t1)
        {
            m_envwidget->setHorizontalViewRange(t0,t1);
        };
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
