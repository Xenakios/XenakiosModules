// modelCOsc

#include "plugin.hpp"
#include "claudio/Oscillator.h"

class COscModule : public Module
{
public:
enum PARAM_IDS
    {
        PAR_PITCH,
        PAR_DETUNE,
        PAR_LAST
    };
    enum INPUTS
    {
        INPUT_PITCH_IN,
        INPUT_LAST
    };
    enum OUTPUTS
    {
        OUTPUT_AUDIO_LEFT,
        OUTPUT_AUDIO_RIGHT,
        OUTPUT_LAST
    };
    COscModule()
    {
        config(PAR_LAST,INPUT_LAST,OUTPUT_LAST);
        configParam(PAR_PITCH,-48.0,48.0,0.0);
        configParam(PAR_DETUNE,0.0,50.0,1.0);
        oscillator1.addWave(&sawWave);
        oscillator1.setCurrentVoices(1);
        oscillator1.setStereoMix(1.0f);
        oscillator1.setDetuneSpread(1.0f);
        oscillator1.setPan(0.5f);
        oscillator1.setNoteShift(0.0);
        oscillator1.setFineTune(0.0);
        //oscillator1.setFrequency(440.0f);
    }
    double oscPhase = 0.0;
    void process(const ProcessArgs& args) override
    {
        float pitch = params[PAR_PITCH].getValue();
        pitch += inputs[INPUT_PITCH_IN].getVoltage()*12.0f;
        pitch = clamp(pitch,-60.0,60.0);
        float centerfreq = dsp::FREQ_C4*pow(2.0f,1.0f/12.0f*pitch);
        oscillator1.setFrequency(centerfreq);
        float detune = params[PAR_DETUNE].getValue();
        oscillator1.setDetune(detune);
        oscillator1.process();
        float leftSample = oscillator1.left();
        float rightSample = oscillator1.right();
        //leftSample = sin(2*PI/44100.0*centerfreq*oscPhase);
        //rightSample = leftSample;
        //oscPhase+=1.0;
        outputs[OUTPUT_AUDIO_LEFT].setVoltage(leftSample*10.0f);
        outputs[OUTPUT_AUDIO_RIGHT].setVoltage(rightSample*10.0f);
        oscillator1.update();
    }
private:
    Wave sawWave = Wave({ -1.0f, 1.0f });
    Oscillator oscillator1;
};

struct COscWidget : ModuleWidget {
	COscWidget(COscModule* module) 
    {
		setModule(module);
        box.size.x = 255;
        addParam(createParam<RoundHugeBlackKnob>(Vec(20, 20), module, COscModule::PAR_PITCH));
        addParam(createParam<RoundBlackKnob>(Vec(100, 20), module, COscModule::PAR_DETUNE));
        addOutput(createOutput<PJ301MPort>(Vec(20, 110), module, COscModule::OUTPUT_AUDIO_LEFT));
        addOutput(createOutput<PJ301MPort>(Vec(50, 110), module, COscModule::OUTPUT_AUDIO_RIGHT));
        addInput(createInput<PJ301MPort>(Vec(80, 110), module, COscModule::INPUT_PITCH_IN));
	}
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);
        /*
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
        */
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelCOsc = createModel<COscModule, COscWidget>("XCOscTest");
