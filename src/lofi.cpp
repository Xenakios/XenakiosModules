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
        if (outrate>inrate)
            outrate = inrate;
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
        configParam(PAR_DRIVE,0.0,1.0,0.0,"Drive");
        configParam(PAR_DISTORTTYPE,0,2.0,0,"Distortion type");
    }
    float getBitDepthFromNormalized(float x)
    {
        if (x>=0.1 && x<=0.9)
            return rescale(x,0.1f,0.9f,2.0f,7.0f);
        if (x<0.1f)
            return rescale(x,0.0f,0.1f,1.0f,2.0f);
        if (x>0.9f)
            return rescale(x,0.9,1.0f,7.0f,16.0f); 
        return 16.0f;           
    }
    float distort(float in, float th, int type)
    {
        if (type == 0)
            return clamp(in,-th,th);
        else if (type == 1)
            return reflect_value(-th,in,th);
        else if (type == 2)
            return wrap_value(-th,in,th);
        return in;
    }
    void process(const ProcessArgs& args) override
    {
        float insample = inputs[IN_AUDIO].getVoltage()/5.0f;
        float drivegain = params[PAR_DRIVE].getValue();
        drivegain += inputs[IN_CV_DRIVE].getVoltage()/5.0f;
        drivegain = clamp(drivegain,0.0f,1.0f);
        drivegain = rescale(drivegain,0.0f,1.0f,-12.0,64.0f);
        drivegain = dsp::dbToAmplitude(drivegain);
        float driven = drivegain*insample;
        driven = distort(driven,1.0f,params[PAR_DISTORTTYPE].getValue());
        float srdiv = params[PAR_RATEDIV].getValue(); 
        srdiv += inputs[IN_CV_RATEDIV].getVoltage()/5.0f;
        srdiv = clamp(srdiv,0.0f,1.0f);
        srdiv = std::pow(srdiv,2.0f);
        srdiv = 1.0+srdiv*99.0;
        m_reducer.setRates(args.sampleRate,args.sampleRate/srdiv);
        float reduced = m_reducer.process(driven);
        float bits = params[PAR_BITDIV].getValue();
        bits += inputs[IN_CV_BITDIV].getVoltage()/5.0f;
        bits = clamp(bits,0.0f,1.0f);
        bits = getBitDepthFromNormalized(bits);
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
        addParam(createParamCentered<RoundBlackKnob>(Vec(30.00, 80), m, XLOFI::PAR_RATEDIV));
        addInput(createInputCentered<PJ301MPort>(Vec(60, 80), m, XLOFI::IN_CV_RATEDIV));
        addParam(createParamCentered<RoundBlackKnob>(Vec(30.00, 120), m, XLOFI::PAR_BITDIV));
        addInput(createInputCentered<PJ301MPort>(Vec(60, 120), m, XLOFI::IN_CV_BITDIV));
        addParam(createParamCentered<RoundBlackKnob>(Vec(30.00, 160), m, XLOFI::PAR_DRIVE));
        addInput(createInputCentered<PJ301MPort>(Vec(60, 160), m, XLOFI::IN_CV_DRIVE));
        addParam(createParamCentered<RoundBlackKnob>(Vec(30.00, 200), m, XLOFI::PAR_DISTORTTYPE));

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
