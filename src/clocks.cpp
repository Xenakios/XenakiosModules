#include "clocks.h"

extern std::shared_ptr<Font> g_font;

RandomClockModule::RandomClockModule()
{
    config(PAR_LAST,1,16);
    configParam(0,0.0f,1.0f,0.1,"Master density"); // master clock density
    float defmult = rescale(1.0f,0.1f,10.0f,0.0f,1.0f);
    for (int i=0;i<8;++i)
    {
        configParam(PAR_DENSITY_MULTIP+i,0.0f,1.0f,defmult,"Density multiplier "+std::to_string(i+1)); // clock multiplier
        // gate len
        // >=0.0 && <=0.5 deterministic percentage 1% to 99% of clock interval
        // >0.5 && <=1.0 stochastic distribution favoring short and long values
        configParam(PAR_GATE_LEN+i,0.0,1.0f,0.25f,"Gate length "+std::to_string(i+1)); 
    }
}

void RandomClockModule::process(const ProcessArgs& args)
{
    float masterdensity = params[PAR_MASTER_DENSITY].getValue();
    if (masterdensity<0.5f)
    {
        masterdensity = rescale(masterdensity,0.0f,0.5f,0.0f,1.0f);
        masterdensity = pow(masterdensity,0.7f);
        masterdensity = rescale(masterdensity,0.0f,1.0f,0.05f,1.0f);
    }
    else
    {
        masterdensity = rescale(masterdensity,0.5f,1.0f,0.0f,1.0f);
        masterdensity = pow(masterdensity,4.0f);
        masterdensity = rescale(masterdensity,0.0f,1.0f,1.0f,200.0f);
    }
    m_curDensity = masterdensity;
    
    for (int i=0;i<8;++i)
    {
        if (outputs[i].isConnected())
        {
            float multip = rescale(params[PAR_DENSITY_MULTIP+i].getValue(),0.0f,1.0f,0.1f,10.0f);
            m_clocks[i].setDensity(masterdensity*multip);
            float glen = params[PAR_GATE_LEN+i].getValue();
            m_clocks[i].setGateLen(glen);
            outputs[i].setVoltage(10.0f*m_clocks[i].process(args.sampleTime));    
        }
        if (outputs[i+8].isConnected())
        {
            outputs[i+8].setVoltage(m_clocks[i].getCurrentInterval());
        }
    }
    
}

RandomClockWidget::RandomClockWidget(RandomClockModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = 130;
    m_mod = m;
    for (int i=0;i<8;++i)
    {
        addOutput(createOutput<PJ301MPort>(Vec(5,30+30*i), module, i));
        addParam(createParam<RoundSmallBlackKnob>(Vec(35, 30+30*i), module, RandomClockModule::PAR_DENSITY_MULTIP+i)); 
        addParam(createParam<RoundSmallBlackKnob>(Vec(65, 30+30*i), module, RandomClockModule::PAR_GATE_LEN+i)); 
        addOutput(createOutput<PJ301MPort>(Vec(95,30+30*i), module, i+8));
    }
    addParam(createParam<RoundBlackKnob>(Vec(5, 30+30*8), module, RandomClockModule::PAR_MASTER_DENSITY));    
    
}

void RandomClockWidget::draw(const DrawArgs &args)
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
    nvgText(args.vg, 3 , 10, "Random clock", NULL);
    char buf[100];
    if (m_mod)
        sprintf(buf,"Xenakios %f",m_mod->m_curDensity);
    else
    {
        sprintf(buf,"Xenakios");
    }
    
    nvgText(args.vg, 3 , h-11, buf, NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}

DividerClockWidget::DividerClockWidget(DivisionClockModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = 290;
    for (int i=0;i<8;++i)
    {
        addParam(createParam<RoundSmallBlackKnob>(Vec(5, 30+30*i), module, i)); 
        addInput(createInput<PJ301MPort>(Vec(35,30+30*i), module, i+1));
        addParam(createParam<RoundSmallBlackKnob>(Vec(65, 30+30*i), module, i+8)); 
        addInput(createInput<PJ301MPort>(Vec(95,30+30*i), module, i+9));
        addParam(createParam<RoundSmallBlackKnob>(Vec(125, 30+30*i), module, i+16)); 
        addInput(createInput<PJ301MPort>(Vec(155,30+30*i), module, i+17));
        addParam(createParam<RoundSmallBlackKnob>(Vec(185, 30+30*i), module, i+24)); 
        addOutput(createOutput<PJ301MPort>(Vec(225,30+30*i), module, i));
        addOutput(createOutput<PJ301MPort>(Vec(255,30+30*i), module, i+8));
    }
    addInput(createInput<PJ301MPort>(Vec(5,30+30*8), module, 0));
    addParam(createParam<RoundLargeBlackKnob>(Vec(65, 30+30*8), module, 32)); 
}

void DividerClockWidget::draw(const DrawArgs &args)
{
    nvgSave(args.vg);
    float w = box.size.x;
    float h = box.size.y;
    nvgBeginPath(args.vg);
    nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
    nvgRect (args.vg,0.0f,0.0f,w,h);
    nvgFill(args.vg);

    nvgFontSize(args.vg, 15);
    nvgFontFaceId(args.vg, g_font->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3 , 10, "Divider clock", NULL);
    
    
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}