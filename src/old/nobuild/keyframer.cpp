#include "keyframer.h"

KeyFramerModule::KeyFramerModule()
{
    config(10,1,8);
    configParam(0,0.0f,1.0f,0.0f,"Frame");
    configParam(9,2.0f,32.0f,32.0f,"Num active frames");
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

json_t* KeyFramerModule::dataToJson()
{
    json_t* resultJ = json_object();
    json_t* arrayJ = json_array();
    for (int i=0;i<(int)NUMSNAPSHOTS;++i)
    {
        json_t* array2J = json_array();
        for (int j=0;j<8;++j)
        {
            json_array_append(array2J,json_real(m_scenes[i][j]));
        }
        json_array_append(arrayJ,array2J);
    }
    json_object_set(resultJ,"snapshots_v1",arrayJ);
    return resultJ;
}

void KeyFramerModule::dataFromJson(json_t* root)
{
    json_t* arrayJ = json_object_get(root,"snapshots_v1");
    if (arrayJ)
    {
        for (int i=0;i<NUMSNAPSHOTS;++i)
        {
            json_t* array2J = json_array_get(arrayJ,i);
            if (array2J)
            {
                for (int j=0;j<8;++j)
                {
                    float v = json_number_value(json_array_get(array2J,j));
                    m_scenes[i][j]=v;
                }
            }
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
		nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
		nvgTextLetterSpacing(args.vg, -2);
		//float w = box.size.x;
		//float h = box.size.y;
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
        nvgText(args.vg, 5.0f ,140.0f, m_last_action.c_str(), NULL);
        nvgRestore(args.vg);
    }
    void onButton(const event::Button& e) override
    {
        //float w = box.size.x;
        float boxsize = 30.0f;
        if (e.action==GLFW_PRESS)
        {
            int x = (e.pos.x/boxsize);
            int y = (e.pos.y/boxsize);
            int index = x+y*8;
            if (e.mods & GLFW_MOD_SHIFT)
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

class MyRoundHugeBlackKnob : public RoundHugeBlackKnob
{
public:
    MyRoundHugeBlackKnob()
    {

    }
    void draw(const DrawArgs& args) override
    {
        RoundHugeBlackKnob::draw(args);
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg,10.0f,10.0f,10.0f);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0xee, 0x00, 0xff));
        nvgFill(args.vg);
        nvgRestore(args.vg);
    }
};

KeyFramerWidget::KeyFramerWidget(KeyFramerModule* m)
{
    setModule(m);
    box.size.x = 255;
    addInput(createInput<PJ301MPort>(Vec(70, 35), module, 0));
    auto bigknob = createParam<MyRoundHugeBlackKnob>(Vec(5, 30), module, 0);
    addParam(bigknob);
    addParam(createParam<RoundHugeBlackKnob>(Vec(95, 30), module, 9));
    for (int i=0;i<8;++i)
    {
        addParam(createParam<RoundSmallBlackKnob>(Vec(5+30*i, 300), module, i+1));    
        addOutput(createOutput<PJ301MPort>(Vec(5+30*i, 330), module, i));
    }
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
    nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3 , 10, "KeyFramer", NULL);
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}





