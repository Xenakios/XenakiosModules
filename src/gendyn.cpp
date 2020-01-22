#include "gendyn.h"

extern std::shared_ptr<Font> g_font;

GendynModule::GendynModule()
{
    config(PARAMS::LASTPAR,0,1);
    configParam(PAR_NumSegments,3.0,64.0,10.0,"Num segments");
    configParam(PAR_TimeMean,-5.0,5.0,0.0,"Time mean");
    configParam(PAR_TimeDeviation,0.0,5.0,0.1,"Time deviation");
    configParam(PAR_TimeSecondaryBarrierLow,1.0,64.0,5.0,"Time sec low barrier");
    configParam(PAR_TimeSecondaryBarrierHigh,2.0,64.0,20.0,"Time sec high barrier");
}
    
void GendynModule::process(const ProcessArgs& args)
{
    float outsample = 0.0f;
    m_osc.setNumSegments(params[PAR_NumSegments].getValue());
    m_osc.m_time_dev = params[PAR_TimeDeviation].getValue();
    m_osc.m_time_mean = params[PAR_TimeMean].getValue();
    float bar0 = params[PAR_TimeSecondaryBarrierLow].getValue();
    float bar1 = params[PAR_TimeSecondaryBarrierHigh].getValue();
    if (bar1<=bar0)
        bar1=bar0+1.0;
    m_osc.m_time_secondary_low_barrier = bar0;
    m_osc.m_time_secondary_high_barrier = bar1;
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
        addParam(createParam<BefacoTinyKnob>(Vec(200, 30+i*30), module, i));    
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
    for (int i=0;i<module->paramQuantities.size();++i)
    {
        nvgText(args.vg, 70 , 50+i*30, module->paramQuantities[i]->getLabel().c_str(), NULL);
    }
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}
