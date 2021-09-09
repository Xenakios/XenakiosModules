#include "gendyn.h"

extern std::shared_ptr<Font> g_font;

GendynModule::GendynModule()
{
    for (int i=0;i<16;++i)
        m_oscs[i].setRandomSeed(i);
    config(PARAMS::LASTPAR,PARAMS::LASTPAR+1,2);
    configParam(PAR_NumSegments,3.0,64.0,10.0,"Num segments");
    configParam(PAR_TimeDistribution,0.0,LASTDIST-1,1.0,"Time distribution");
    configParam(PAR_TimeMean,-5.0,5.0,0.0,"Time mean");
    configParam(PAR_TimeResetMode,0.0,LASTRM,RM_Avg,"Time reset mode");
    configParam(PAR_TimeDeviation,0.0,5.0,0.1,"Time deviation");
    configParam(PAR_TimePrimaryBarrierLow,-5.0,5.0,-1.0,"Time primary low barrier");
    configParam(PAR_TimePrimaryBarrierHigh,-5.0,5.0,1.0,"Time primary high barrier");
    configParam(PAR_TimeSecondaryBarrierLow,-60.0,60.0,-1.0,"Time sec low barrier");
    configParam(PAR_TimeSecondaryBarrierHigh,-60.0,60.0,1.0,"Time sec high barrier");
    configParam(PAR_AmpResetMode,0.0,LASTRM,RM_UniformRandom,"Amp reset mode");
    configParam(PAR_PolyphonyVoices,1.0,16.0,1,"Polyphony voices");
    configParam(PAR_CenterFrequency,-54.f, 54.f, 0.f, "Center frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
    // configParam(FREQ_PARAM, -54.f, 54.f, 0.f, "Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
    m_divider.setDivision(32);
}

std::string GendynModule::getDebugMessage()
{
    std::stringstream ss;
    ss << m_oscs[0].m_low_frequency << " " << m_oscs[0].m_center_frequency << " ";
    ss << m_oscs[0].m_high_frequency << " " << m_oscs[0].m_time_secondary_low_barrier << " ";
    ss << m_oscs[0].m_time_secondary_high_barrier;
    return ss.str();
    
}

void GendynModule::process(const ProcessArgs& args)
{
    int numvoices = params[PAR_PolyphonyVoices].getValue();
    numvoices = clamp(numvoices,1,16);
    bool shouldReset = false;
    if (m_reset_trigger.process(inputs[0].getVoltage()))
    {
        shouldReset = true;
        
        
    }
    outputs[0].setChannels(numvoices);
    outputs[1].setChannels(numvoices);
    float numsegs = params[PAR_NumSegments].getValue();
    numsegs+=rescale(inputs[1+PAR_NumSegments].getVoltage(),0.0f,10.0f,0,61);
    numsegs=clamp(numsegs,3.0,64.0);
    float timedev = params[PAR_TimeDeviation].getValue();
    timedev+=rescale(inputs[1+PAR_TimeDeviation].getVoltage(),0.0f,10.0f,0.0f,5.0f);
    timedev=clamp(timedev,0.0f,5.0f);
    float sectimebarlow = params[PAR_TimeSecondaryBarrierLow].getValue();
    sectimebarlow+=rescale(inputs[1+PAR_TimeSecondaryBarrierLow].getVoltage(),0.0f,10.0f,1.0,64.0);
    sectimebarlow=clamp(sectimebarlow,1.0,64.0);
    float sectimebarhigh = params[PAR_TimeSecondaryBarrierHigh].getValue();
    sectimebarhigh+=rescale(inputs[1+PAR_TimeSecondaryBarrierHigh].getVoltage(),0.0f,10.0f,1.0,64.0);
    sectimebarhigh=clamp(sectimebarhigh,1.0,64.0);
    sanitizeRange(sectimebarlow,sectimebarhigh,1.0f);
    
    if (m_divider.process())
    {
        int numpitchins = inputs[1+PAR_CenterFrequency].getChannels();
        if (numpitchins<1)
            numpitchins = 1;
        for (int i=0;i<numvoices;++i)
        {
            m_oscs[i].m_sampleRate = args.sampleRate;
            
            m_oscs[i].setNumSegments(numsegs);
            m_oscs[i].m_time_dev = timedev;
            m_oscs[i].m_time_mean = params[PAR_TimeMean].getValue();
            float pitch = params[PAR_CenterFrequency].getValue();
            pitch += rescale(inputs[1+PAR_CenterFrequency].getVoltage(i % numpitchins),
                -5.0f,5.0f,-60.0f,60.0f);
            pitch = clamp(pitch,-60.0f,60.0f);
            float centerfreq = dsp::FREQ_C4*pow(2.0f,1.0f/12.0f*pitch);
            m_oscs[i].setFrequencies(centerfreq,params[PAR_TimeSecondaryBarrierLow].getValue(),
                params[PAR_TimeSecondaryBarrierHigh].getValue());
            //m_oscs[i].m_time_secondary_low_barrier = sectimebarlow;
            //m_oscs[i].m_time_secondary_high_barrier = sectimebarhigh;
            float bar0 = params[PAR_TimePrimaryBarrierLow].getValue();
            float bar1 = params[PAR_TimePrimaryBarrierHigh].getValue();
            if (bar1<=bar0)
                bar1=bar0+0.01;
            m_oscs[i].m_time_primary_low_barrier = bar0;
            m_oscs[i].m_time_primary_high_barrier = bar1;
        }
    }
    if (shouldReset == true)
    {
        for (int i=0;i<numvoices;++i)
        {
            m_oscs[i].m_ampResetMode = params[PAR_AmpResetMode].getValue();
            m_oscs[i].m_timeResetMode = params[PAR_TimeResetMode].getValue();
            float pitch = params[PAR_CenterFrequency].getValue();
            pitch+=rescale(inputs[1+PAR_CenterFrequency].getVoltage(i),-5.0f,5.0f,-60.0f,60.0f);
            pitch = clamp(pitch,-60.0f,60.0f);
            float centerfreq = dsp::FREQ_C4*pow(2.0f,1.0f/12.0f*pitch);
            m_oscs[i].setFrequencies(centerfreq,params[PAR_TimeSecondaryBarrierLow].getValue(),
                params[PAR_TimeSecondaryBarrierHigh].getValue());
            m_oscs[i].resetTable();
        }
    }
    for (int i=0;i<numvoices;++i)
    {
        float outsample = 0.0f;
        m_oscs[i].process(&outsample,1);
        
        outputs[1].setVoltage(m_oscs[i].m_curFrequencyVolts,i);
        outputs[0].setVoltage(outsample*5.0f,i);
    }
    dsp::SampleRateConverter<8> rs;
    
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
        int xpos = i / 11;
        int ypos = i % 11;
        BefacoTinyKnob* knob = nullptr;
        addParam(knob = createParam<BefacoTinyKnob>(Vec(220+250*xpos, 30+ypos*30), module, i)); 
        if (i == GendynModule::PAR_PolyphonyVoices)
            knob->snap = true;
        addInput(createInput<PJ301MPort>(Vec(250+250*xpos, 30+ypos*30), module, 1+i));
        auto* atveknob = new Trimpot;
        atveknob->box.pos.x = 280+250*xpos;
        atveknob->box.pos.y = 33+ypos*30;
        //atveknob->box.size.x = 15;
        //atveknob->box.size.y = 15;
        addParam(atveknob);
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
        for (int i=0;i<(int)module->paramQuantities.size();++i)
        {
            int xpos = i / 11;
            int ypos = i % 11;
            nvgText(args.vg, 70+250*xpos , 50+ypos*30, module->paramQuantities[i]->getLabel().c_str(), NULL);
        }
        GendynModule* mod = dynamic_cast<GendynModule*>(module);
        nvgText(args.vg, 1 , 20, mod->getDebugMessage().c_str(), NULL);
    }
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}
