#include "gendyn.h"

extern std::shared_ptr<Font> g_font;

GendynModule::GendynModule()
{
    config(PARAMS::LASTPAR,0,1);
    configParam(PAR_NumSegments,3.0,64.0,10.0);
    configParam(PAR_TimeDeviation,0.0,5.0,0.1);
}
    
void GendynModule::process(const ProcessArgs& args)
{
    float outsample = 0.0f;
    m_osc.setNumSegments(params[PAR_NumSegments].getValue());
    m_osc.m_time_dev = params[PAR_TimeDeviation].getValue();
    m_osc.process(&outsample,1);
    outputs[0].setVoltage(outsample*10.0f);
}

GendynWidget::GendynWidget(GendynModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = 255;
    addOutput(createOutput<PJ301MPort>(Vec(30, 30), module, 0));
    for (int i=0;i<GendynModule::LASTPAR;++i)
    {
        addParam(createParam<RoundSmallBlackKnob>(Vec(60, 30+i*30), module, i));    
    }
}

void GendynWidget::draw(const DrawArgs &args)
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
    nvgText(args.vg, 3 , 10, "GenDyn", NULL);
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}
