#include "audiostretcher.h"
#include <fstream>

#ifdef RBMODULE

extern std::shared_ptr<Font> g_font;

AudioStretchModule::AudioStretchModule()
{
    config(PAR_LAST,INPUT_LAST,OUTPUT_LAST);
    configParam(PAR_PITCH_SHIFT,-12.0f,12.0f,0.0f);
    configParam(PAR_RESET,0,1,0);
    m_paramdiv.setDivision(16);
    m_lastnumchans = 0;
    RubberBand::RubberBandStretcher::setDefaultDebugLevel(0);
    for (int i=0;i<16;++i)
    {
        m_procinbufs[i].resize(m_procbufsize);
        m_procoutbufs[i].resize(m_procbufsize);
        m_st[i].reset(
            new RubberBand::RubberBandStretcher(44100,1,
            RubberBand::RubberBandStretcher::Option::OptionProcessRealTime
            |RubberBand::RubberBandStretcher::Option::OptionPitchHighConsistency));
        //m_st[i]->setMaxProcessSize(1);
    }
    
}

void AudioStretchModule::process(const ProcessArgs& args)
{
    if (m_st[0]==nullptr)
        return;
    float insample = inputs[INPUT_AUDIO_IN].getVoltageSum();
    float* procinbuf[1] = {&insample};
    int numpolychans = std::max(1,inputs[INPUT_PITCH_IN].getChannels());
    if (m_paramdiv.process())
    {
        bool rst = params[PAR_RESET].getValue();
        if (m_lastnumchans!=numpolychans || m_lastreset!=rst)
        {
            m_lastreset = rst;
            m_lastnumchans = numpolychans;
            for (int i=0;i<16;++i)
            {
                m_st[i]->reset();
            }
        }
        float semitones = params[PAR_PITCH_SHIFT].getValue();
        for (int i=0;i<numpolychans;++i)
        {
            semitones += rescale(inputs[INPUT_PITCH_IN].getVoltage(i),-5.0f,5.0f,-12.0f,12.0f);
            semitones = clamp(semitones,-36.0f,36.0f);
            m_st[i]->setPitchScale(pow(2.0,semitones/12.0));
        }
        outputs[OUTPUT_AUDIO_OUT].setChannels(numpolychans);
        outputs[OUTPUT_BUFFERAMOUNT].setChannels(numpolychans);
    }
    float retrbuf[1024];
    for (int i=0;i<numpolychans;++i)
    {
        m_st[i]->process(procinbuf,1,false);
        float outsample = 0.0f;
        float bufvolt = rescale(m_st[i]->available(),0,16384,0.0f,10.0f);
        outputs[OUTPUT_BUFFERAMOUNT].setVoltage(bufvolt,i);
        if (m_st[i]->available()>=1024)
        {
            float* procoutbuf[1] = {&outsample};
            m_st[i]->retrieve(procoutbuf,1);
            outputs[OUTPUT_AUDIO_OUT].setVoltage(outsample,i);
        }
        
        
    }
    
}

AudioStretchWidget::AudioStretchWidget(AudioStretchModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = 255;
    addParam(createParam<RoundHugeBlackKnob>(Vec(20, 20), module, AudioStretchModule::PAR_PITCH_SHIFT));
    addParam(createParam<LEDButton>(Vec(160, 20), module, AudioStretchModule::PAR_RESET));
    addInput(createInput<PJ301MPort>(Vec(20, 100), module, AudioStretchModule::INPUT_AUDIO_IN));
    addInput(createInput<PJ301MPort>(Vec(50, 100), module, AudioStretchModule::INPUT_PITCH_IN));
    
    addOutput(createOutput<PJ301MPort>(Vec(80, 100), module, AudioStretchModule::OUTPUT_AUDIO_OUT));
    addOutput(createOutput<PJ301MPort>(Vec(110, 100), module, AudioStretchModule::OUTPUT_BUFFERAMOUNT));
}

void AudioStretchWidget::draw(const DrawArgs &args)
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
    auto rbmod = dynamic_cast<AudioStretchModule*>(module);
    if (rbmod)
    {
        char buf[1024];
        sprintf(buf,"AudioStretcher %d",rbmod->m_rbAvailableOutput);
        nvgText(args.vg, 3 , 10, buf, NULL);
    }
    
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}
#endif
