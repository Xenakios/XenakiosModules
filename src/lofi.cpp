#include "plugin.hpp"

class SampleRateReducer
{
public:
    float process(float insample)
    {
        phase+=1.0;
        if (phase>=divlen)
        {
            phase -= divlen;
            heldsample = insample;
        }
        return heldsample;
    }
    void setRates(float inrate, float outrate)
    {
        divlen = inrate/outrate;
    }
private:
    float heldsample = 0.0f;
    double phase = 0.0;
    double divlen = 1.0;
};

class XLOFI : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_RATEDIV,
        PAR_BITDIV,
        PAR_DRIVE,
        PAR_DISTORTTYPE,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_AUDIO,
        IN_CV_RATEDIV,
        IN_CV_BITDIV,
        IN_CV_DRIVE,
        LAST_INPUT
    };
    enum OUTPUTS
    {
        OUT_AUDIO,
        LAST_OUTPUT
    };
    XLOFI()
    {
        config(PAR_LAST,LAST_INPUT,LAST_OUTPUT);
        configParam(PAR_RATEDIV,0.0,1.0,0.0,"Sample rate reduction");
        configParam(PAR_BITDIV,0.0,1.0,1.0,"Bit depth");
        configParam(PAR_DRIVE,-12.0,96.0,0.0,"Drive (db)");
        configParam(PAR_DISTORTTYPE,0,2.0,0,"Distortion type");
    }
    void process(const ProcessArgs& args) override
    {
        float insample = inputs[IN_AUDIO].getVoltage()/5.0f;
        float drivegain = dsp::dbToAmplitude(params[PAR_DRIVE].getValue());
        float driven = drivegain*insample;
        driven = reflect_value(-1.0f,driven,1.0f);
        float srdiv = std::pow(params[PAR_RATEDIV].getValue(),2.0f);
        srdiv = 1.0+srdiv*99.0;
        m_reducer.setRates(args.sampleRate,args.sampleRate/srdiv);
        float reduced = m_reducer.process(driven);
        float bits = params[PAR_BITDIV].getValue();
        bits = 1.0f+15.0f*std::pow(bits,4.0);
        bits = clamp(bits,1.0,16.0);
        float bitlevels = std::round(std::pow(2.0f,17.0-bits))-1.0f;
        float crushed = reduced*32767.0;
        crushed = std::round(crushed/bitlevels)*bitlevels;
        crushed /= 32767.0;
        crushed = clamp(crushed,-1.0f,1.0f);
        float outsample = crushed*5.0f;
        outputs[OUT_AUDIO].setVoltage(outsample);
    }
private:
    SampleRateReducer m_reducer;
};

class XLOFIWidget : public ModuleWidget
{
public:
    XLOFIWidget(XLOFI* m)
    {
        setModule(m);
        box.size.x = 80;
        addInput(createInputCentered<PJ301MPort>(Vec(30, 30), m, XLOFI::IN_AUDIO));
        addOutput(createOutputCentered<PJ301MPort>(Vec(60, 30), m, XLOFI::IN_AUDIO));
        addParam(createParamCentered<RoundBlackKnob>(Vec(40.00, 80), m, XLOFI::PAR_RATEDIV));
        addParam(createParamCentered<RoundBlackKnob>(Vec(40.00, 120), m, XLOFI::PAR_BITDIV));
        addParam(createParamCentered<RoundBlackKnob>(Vec(40.00, 160), m, XLOFI::PAR_DRIVE));
        addParam(createParamCentered<RoundBlackKnob>(Vec(40.00, 200), m, XLOFI::PAR_DISTORTTYPE));

    }
    void draw(const DrawArgs &args)
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);

        
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
}
};

Model* modelXLOFI = createModel<XLOFI, XLOFIWidget>("XLOFI");
