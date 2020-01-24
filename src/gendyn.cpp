#include "gendyn.h"

extern std::shared_ptr<Font> g_font;

inline double custom_log(double value, double base)
{
    return std::log(value)/std::log(base);
}

GendynModule::GendynModule()
{
    config(PARAMS::LASTPAR,1,2);
    configParam(PAR_NumSegments,3.0,64.0,10.0,"Num segments");
    configParam(PAR_TimeDistribution,0.0,LASTDIST-1,1.0,"Time distribution");
    configParam(PAR_TimeMean,-5.0,5.0,0.0,"Time mean");
    configParam(PAR_TimeResetMode,0.0,LASTRM,RM_Avg,"Time reset mode");
    configParam(PAR_TimeDeviation,0.0,5.0,0.1,"Time deviation");
    configParam(PAR_TimePrimaryBarrierLow,-5.0,5.0,-1.0,"Time primary low barrier");
    configParam(PAR_TimePrimaryBarrierHigh,-5.0,5.0,1.0,"Time primary high barrier");
    configParam(PAR_TimeSecondaryBarrierLow,1.0,64.0,5.0,"Time sec low barrier");
    configParam(PAR_TimeSecondaryBarrierHigh,2.0,64.0,20.0,"Time sec high barrier");
    configParam(PAR_AmpResetMode,0.0,LASTRM,RM_UniformRandom,"Amp reset mode");
}
    
void GendynModule::process(const ProcessArgs& args)
{
    if (m_reset_trigger.process(inputs[0].getVoltage()))
    {
        m_osc.m_ampResetMode = params[PAR_AmpResetMode].getValue();
        m_osc.m_timeResetMode = params[PAR_TimeResetMode].getValue();
        m_osc.resetTable();
    }
    m_osc.m_sampleRate = args.sampleRate;
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
    bar0 = params[PAR_TimePrimaryBarrierLow].getValue();
    bar1 = params[PAR_TimePrimaryBarrierHigh].getValue();
    if (bar1<=bar0)
        bar1=bar0+0.01;
    m_osc.m_time_primary_low_barrier = bar0;
    m_osc.m_time_primary_high_barrier = bar1;
    m_osc.process(&outsample,1);
    outputs[0].setVoltage(outsample*10.0f);
    float volts = custom_log(m_osc.m_curFrequency/rack::dsp::FREQ_C4,2.0f);
    volts = clamp(volts,-5.0,5.0);
    outputs[1].setVoltage(volts);
}

GendynWidget::GendynWidget(GendynModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = 600;
    addOutput(createOutput<PJ301MPort>(Vec(30, 30), module, 0));
    addOutput(createOutput<PJ301MPort>(Vec(30, 60), module, 1));
    addInput(createInput<PJ301MPort>(Vec(30, 90), module, 0));
    for (int i=0;i<GendynModule::LASTPAR;++i)
    {
        int xpos = i / 9;
        int ypos = i % 9;
        addParam(createParam<BefacoTinyKnob>(Vec(220+250*xpos, 30+ypos*30), module, i));    
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
    if (module)
    {
        for (int i=0;i<module->paramQuantities.size();++i)
        {
            int xpos = i / 9;
            int ypos = i % 9;
            nvgText(args.vg, 70+250*xpos , 50+ypos*30, module->paramQuantities[i]->getLabel().c_str(), NULL);
        }
    }
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}
