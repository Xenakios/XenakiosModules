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

inline float distort(float in, float th, float type)
{
    float distsamples[5];
    distsamples[0] = soft_clip(in);
    distsamples[1] = clamp(in,-th,th);
    distsamples[2] = reflect_value(-th,in,th);
    distsamples[3] = wrap_value(-th,in,th);
    distsamples[4] = distsamples[3];
    int index0 = std::floor(type);
    int index1 = index0+1;
    float frac = type-index0;
    float y0 = distsamples[index0];
    float y1 = distsamples[index1];
    return y0+(y1-y0)*frac;
}

inline float getBitDepthFromNormalized(float x)
{
    if (x>=0.1 && x<=0.9)
        return rescale(x,0.1f,0.9f,2.0f,7.0f);
    if (x<0.1f)
        return rescale(x,0.0f,0.1f,1.0f,2.0f);
    if (x>0.9f)
        return rescale(x,0.9,1.0f,7.0f,16.0f); 
    return 16.0f;           
}

class LOFIEngine
{
public:
    LOFIEngine()
    {}
    float process(float in, float insamplerate, float srdiv, float bits, float drive, float dtype, float oversample)
    {
        float driven = drive*in;
        float oversampledriven = 0.0f;
        if (oversample>0.0f) // only oversample when oversampled signal is going to be mixed in
        {
            float osarr[8];
            m_upsampler.process(driven,osarr);
            for (int i=0;i<8;++i)
                osarr[i] = distort(osarr[i],1.0f,dtype);
            oversampledriven = 2.0f*m_downsampler.process(osarr);
        }
        
        driven = distort(driven,1.0f,dtype);
        float drivemix = (1.0f-oversample) * driven + oversample * oversampledriven;
        m_reducer.setRates(insamplerate,insamplerate/srdiv);
        float reduced = m_reducer.process(drivemix);
        bits = getBitDepthFromNormalized(bits);
        float bitlevels = std::round(std::pow(2.0f,17.0-bits))-1.0f;
        float crushed = reduced*32767.0;
        crushed = std::round(crushed/bitlevels)*bitlevels;
        crushed /= 32767.0;
        return clamp(crushed,-1.0f,1.0f);
    }
private:
    SampleRateReducer m_reducer;
    dsp::Upsampler<8,2> m_upsampler;
    dsp::Decimator<8,2> m_downsampler;
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
        PAR_ATTN_RATEDIV,
        PAR_ATTN_BITDIV,
        PAR_ATTN_DRIVE,
        PAR_ATTN_DISTYPE,
        PAR_OVERSAMPLE,
        PAR_ATTN_OVERSAMPLE,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_AUDIO,
        IN_CV_RATEDIV,
        IN_CV_BITDIV,
        IN_CV_DRIVE,
        IN_CV_DISTTYPE,
        IN_CV_OVERSAMPLE,
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
        configParam(PAR_DRIVE,0.0,1.0,0.15,"Drive");
        configParam(PAR_DISTORTTYPE,0,3,0,"Distortion type");
        configParam(PAR_ATTN_RATEDIV,-1.0f,1.0f,0.0,"Sample rate reduction CV");
        configParam(PAR_ATTN_BITDIV,-1.0f,1.0f,0.0,"Bit depth CV");
        configParam(PAR_ATTN_DRIVE,-1.0f,1.0f,0.0,"Drive CV");
        configParam(PAR_ATTN_DISTYPE,-1.0f,1.0f,0.0,"Distortion type CV");
        configParam(PAR_OVERSAMPLE,0.0f,1.0f,0.0,"Distortion oversampling mix");
        configParam(PAR_ATTN_OVERSAMPLE,-1.0f,1.0f,0.0,"Distortion oversampling mix CV");
    }
    
    
    void process(const ProcessArgs& args) override
    {
        float insample = inputs[IN_AUDIO].getVoltage()/5.0f;
        float drivegain = params[PAR_DRIVE].getValue();
        drivegain += inputs[IN_CV_DRIVE].getVoltage()*params[PAR_ATTN_DRIVE].getValue()/10.0f;
        drivegain = clamp(drivegain,0.0f,1.0f);
        drivegain = rescale(drivegain,0.0f,1.0f,-12.0,52.0f);
        drivegain = dsp::dbToAmplitude(drivegain);
        float dtype = params[PAR_DISTORTTYPE].getValue();
        dtype += inputs[IN_CV_DISTTYPE].getVoltage()*params[PAR_ATTN_DISTYPE].getValue()/3.0f;
        dtype = clamp(dtype,0.0f,3.0f);
        float srdiv = params[PAR_RATEDIV].getValue(); 
        srdiv += inputs[IN_CV_RATEDIV].getVoltage()*params[PAR_ATTN_RATEDIV].getValue()/10.0f;
        srdiv = clamp(srdiv,0.0f,1.0f);
        srdiv = std::pow(srdiv,2.0f);
        srdiv = 1.0+srdiv*99.0;
        float bits = params[PAR_BITDIV].getValue();
        bits += inputs[IN_CV_BITDIV].getVoltage()*params[PAR_ATTN_BITDIV].getValue()/10.0f;
        bits = clamp(bits,0.0f,1.0f);
        float osamt = params[PAR_OVERSAMPLE].getValue();
        osamt += inputs[IN_CV_OVERSAMPLE].getVoltage()*params[PAR_ATTN_OVERSAMPLE].getValue()/10.0f;
        osamt = clamp(osamt,0.0f,1.0f);
        float processed = m_engines[0].process(insample,args.sampleRate,srdiv,bits,drivegain,dtype,osamt);
        outputs[OUT_AUDIO].setVoltage(processed*5.0f);
    }
private:
    
    LOFIEngine m_engines[16];
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
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(15.00, 80), m, XLOFI::PAR_RATEDIV));
        addInput(createInputCentered<PJ301MPort>(Vec(45, 80), m, XLOFI::IN_CV_RATEDIV));
        addParam(createParamCentered<Trimpot>(Vec(70.00, 80), m, XLOFI::PAR_ATTN_RATEDIV));
        
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(15.00, 120), m, XLOFI::PAR_BITDIV));
        addInput(createInputCentered<PJ301MPort>(Vec(45, 120), m, XLOFI::IN_CV_BITDIV));
        addParam(createParamCentered<Trimpot>(Vec(70.00, 120), m, XLOFI::PAR_ATTN_BITDIV));

        addParam(createParamCentered<RoundBlackKnob>(Vec(15.00, 160), m, XLOFI::PAR_DRIVE));
        addInput(createInputCentered<PJ301MPort>(Vec(45, 160), m, XLOFI::IN_CV_DRIVE));
        addParam(createParamCentered<Trimpot>(Vec(70.00, 160), m, XLOFI::PAR_ATTN_DRIVE));
        
        RoundBlackKnob* knob = nullptr;
        addParam(knob = createParamCentered<RoundBlackKnob>(Vec(15.00, 200), m, XLOFI::PAR_DISTORTTYPE));
        addInput(createInputCentered<PJ301MPort>(Vec(45, 200), m, XLOFI::IN_CV_DISTTYPE));
        addParam(createParamCentered<Trimpot>(Vec(70.00, 200), m, XLOFI::PAR_ATTN_DISTYPE));

        addParam(createParamCentered<RoundBlackKnob>(Vec(15.00, 240), m, XLOFI::PAR_OVERSAMPLE));
        addInput(createInputCentered<PJ301MPort>(Vec(45, 240), m, XLOFI::IN_CV_OVERSAMPLE));
        addParam(createParamCentered<Trimpot>(Vec(70.00, 240), m, XLOFI::PAR_ATTN_OVERSAMPLE));
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
