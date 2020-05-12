#include "audiostretcher.h"

extern std::shared_ptr<Font> g_font;

AudioStretchModule::AudioStretchModule()
{
    config(PAR_LAST,INPUT_LAST,OUTPUT_LAST);
    configParam(PAR_PITCH_SHIFT,-12.0f,12.0f,0.0f);
    m_paramdiv.setDivision(512);
    for (int i=0;i<16;++i)
    {
        m_st[i].reset(
            new RubberBand::RubberBandStretcher(44100,1,
            RubberBand::RubberBandStretcher::Option::OptionProcessRealTime
            |RubberBand::RubberBandStretcher::Option::OptionPitchHighConsistency));
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
        if (m_lastnumchans!=numpolychans)
        {
            m_lastnumchans = numpolychans;
            for (int i=0;i<numpolychans;++i)
            {
                //m_st[i]->reset();
            }
        }
        float semitones = params[PAR_PITCH_SHIFT].getValue();
        for (int i=0;i<numpolychans;++i)
        {
            semitones+=rescale(inputs[INPUT_PITCH_IN].getVoltage(i),-5.0f,5.0f,-12.0f,12.0f);
            m_st[i]->setPitchScale(pow(2.0,semitones/12.0));
        }
        outputs[OUTPUT_AUDIO_OUT].setChannels(numpolychans);
    }
    for (int i=0;i<numpolychans;++i)
    {
        m_st[i]->process(procinbuf,1,false);
        float outsample = 0.0f;
        if (m_st[i]->available()>=128)
        {
            float* procoutbuf[1] = {&outsample};
            m_st[i]->retrieve(procoutbuf,1);
        }
        outputs[OUTPUT_AUDIO_OUT].setVoltage(outsample,i);
    }
    
}

AudioStretchWidget::AudioStretchWidget(AudioStretchModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = 255;
    addParam(createParam<RoundHugeBlackKnob>(Vec(20, 20), module, AudioStretchModule::PAR_PITCH_SHIFT));
    addInput(createInput<PJ301MPort>(Vec(20, 100), module, AudioStretchModule::INPUT_AUDIO_IN));
    addInput(createInput<PJ301MPort>(Vec(50, 100), module, AudioStretchModule::INPUT_PITCH_IN));
    addOutput(createOutput<PJ301MPort>(Vec(80, 100), module, AudioStretchModule::OUTPUT_AUDIO_OUT));
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
    nvgText(args.vg, 3 , 10, "AudioStretcher", NULL);
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}
