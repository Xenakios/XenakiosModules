#pragma once

#include <rack.hpp>
#include "plugin.hpp"
#include "RubberBandStretcher.h"

class AudioStretchModule : public rack::Module
{
public:
    enum PARAM_IDS
    {
        PAR_PITCH_SHIFT,
        PAR_LAST
    };
    enum INPUTS
    {
        INPUT_AUDIO_IN,
        INPUT_PITCH_IN,
        INPUT_LAST
    };
    enum OUTPUTS
    {
        OUTPUT_AUDIO_OUT,
        OUTPUT_LAST
    };
    AudioStretchModule();
    void process(const ProcessArgs& args) override;
private:
    std::unique_ptr<RubberBand::RubberBandStretcher> m_st[16];
    dsp::ClockDivider m_paramdiv;
    int m_lastnumchans = 0;
};

class AudioStretchWidget : public ModuleWidget
{
public:
    AudioStretchWidget(AudioStretchModule* m);
    void draw(const DrawArgs &args) override;
    
};
