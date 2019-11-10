#include "keyframer.h"

extern std::shared_ptr<Font> g_font;

KeyFramerModule::KeyFramerModule()
{
    config(10,1,8);
    configParam(0,0.0f,1.0f,0.0f);
    configParam(9,2.0f,32.0f,32.0f);
    for (int i=0;i<8;++i)
    {
        configParam(i+1,0.0f,10.0f,0.0f);
        m_interpolated[i]=0.0f;
        for (int j=0;j<NUMSNAPSHOTS;++j)
        {
            m_scenes[j][i]=0.0f;
        }
    }
}

void KeyFramerModule::process(const ProcessArgs& args)
{
    m_maxsnapshots = params[9].getValue();
    float morphcvdelta = rescale(inputs[0].getVoltage(),0.0,10.0f,0.0f,1.0f);
    float morph = rescale(params[0].getValue()+morphcvdelta,0.0f,1.0,0.0,m_maxsnapshots-1);
    morph = clamp(morph,0.0f,(float)m_maxsnapshots);
    m_cur_morph = morph;
    float fpart = morph-std::floor(morph);
    int ind0 = morph;
    int ind1 = ind0+1;
    if (ind1>m_maxsnapshots-1)
        ind1 = m_maxsnapshots-1;
    for (int i=0;i<8;++i)
    {
        float v0 = m_scenes[ind0][i];
        float v1 = m_scenes[ind1][i];
        float interp = v0+(v1-v0)*fpart;
        outputs[i].setVoltage(interp);
    }
}

class KeyframerSnaphotsWidget : public TransparentWidget
{
public:
    KeyframerSnaphotsWidget(KeyFramerModule* m)
    {
        m_mod = m;
    }
    void draw(const DrawArgs &args) override
    {
        if (!m_mod)
            return;
        nvgSave(args.vg);
		nvgFontSize(args.vg, 13);
		nvgFontFaceId(args.vg, g_font->handle);
		nvgTextLetterSpacing(args.vg, -2);
		float w = box.size.x;
		float h = box.size.y;
		for (int i=0;i<NUMSNAPSHOTS;++i)
        {
            int xpos = i % 8;
            int ypos = i / 8;
            float xcor = 5.0f+30.0f*xpos;
            float ycor = 5.0f+30.0f*ypos;
            nvgBeginPath(args.vg);
            if (i<m_mod->m_maxsnapshots)
		        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            else
                nvgFillColor(args.vg, nvgRGBA(0x40, 0x40, 0x40, 0xff));
		    nvgRect(args.vg,xcor,ycor,20.0f,20.0f);
		    nvgFill(args.vg);
            int curindex = std::floor(m_mod->m_cur_morph);
            if (i == curindex)
            {
                nvgBeginPath(args.vg);
                nvgFillColor(args.vg, nvgRGBA(0x00, 0xee, 0x00, 0xff));
                nvgRect(args.vg,xcor+2.0,ycor,16.0f,3.0f);
                nvgFill(args.vg);
            }
            nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
            char buf[10];
            sprintf(buf,"%d",i+1);
            nvgText(args.vg, xcor+8.0f , ycor+13.0f, buf, NULL);
        }
        nvgText(args.vg, 5.0f , 80.0f, m_last_action.c_str(), NULL);
        nvgRestore(args.vg);
    }
    void onButton(const event::Button& e) override
    {
        float w = box.size.x;
        float boxsize = 30.0f;
        if (e.action==GLFW_PRESS)
        {
            int x = (e.pos.x/boxsize);
            int y = (e.pos.y/boxsize);
            int index = x+y*8;
            if (e.mods==GLFW_MOD_SHIFT)
            {
                m_mod->updateSnapshot(index);
                m_last_action = "update "+std::to_string(index);
            }
            else
            {
                m_mod->recallSnapshot(index);    
                m_last_action = "recall "+std::to_string(index);
            }
            
            
        }
    }

private:
    KeyFramerModule* m_mod = nullptr;
    std::string m_last_action;
};

KeyFramerWidget::KeyFramerWidget(KeyFramerModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = 255;
    addInput(createInput<PJ301MPort>(Vec(70, 35), module, 0));
    auto bigknob = createParam<RoundHugeBlackKnob>(Vec(5, 30), module, 0);
    //bigknob->box.size.x = 200;
    //bigknob->box.size.y = 200;
    addParam(bigknob);
    addParam(createParam<RoundHugeBlackKnob>(Vec(95, 30), module, 9));
    for (int i=0;i<8;++i)
    {
        addParam(createParam<RoundSmallBlackKnob>(Vec(5+30*i, 300), module, i+1));    
        addOutput(createOutput<PJ301MPort>(Vec(5+30*i, 330), module, i));
    }
    /*
    for (int i=0;i<NUMSNAPSHOTS;++i)
    {
        int xpos = i % 8;
        int ypos = i / 8;
        CKD6* but = new CKD6;
        
        but->box.pos.x = 5+xpos*30;
        but->box.pos.y = 90+ypos*30;
        addChild(but);
    }
    */
    KeyframerSnaphotsWidget* sw = new KeyframerSnaphotsWidget(m);
    sw->box.pos = Vec(5.0f,90.0f);
    sw->box.size = Vec(300.0f,150.0f);
    addChild(sw);
}

void KeyFramerWidget::draw(const DrawArgs &args)
{
    nvgSave(args.vg);
    float w = box.size.x;
    float h = box.size.y;
    nvgBeginPath(args.vg);
    nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
    nvgRect(args.vg,0.0f,0.0f,w,h);
    nvgFill(args.vg);

    nvgFontSize(args.vg, 15);
    nvgFontFaceId(args.vg, g_font->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3 , 10, "KeyFramer", NULL);
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}





