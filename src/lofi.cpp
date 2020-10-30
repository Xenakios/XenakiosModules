#include "plugin.hpp"
#include <random>

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

class GlitchGenerator
{
public:
    enum GLITCHES
    {
        GLT_SILENCE,
        GLT_KILOSINE,
        GLT_RINGMOD,
        GLT_NOISE,
        GLT_REPEAT,
        GLT_LAST,
    };
    GlitchGenerator()
    {
        m_gen = std::mt19937((size_t)this);
        m_repeatBuf.resize(65536);
    }
    float process(float in, float samplerate, float density)
    {
        if (m_curglitch!=GLT_REPEAT)
        {
            m_repeatBuf[m_repeatWritePos] = in;
            ++m_repeatWritePos;
            if (m_repeatWritePos>=m_repeatLen)
                m_repeatWritePos = 0;
        }
        
        if (m_phase>=m_nextglitchpos)
        {
            std::uniform_int_distribution<int> dist(0,GLT_LAST-1);
            m_curglitch = (GLITCHES)dist(m_gen);
            if (m_curglitch==GLT_REPEAT)
            {
                m_repeatReadPos = 0;
                std::uniform_real_distribution<float> replendist(0.01,0.08);
                m_repeatLen = samplerate*replendist(m_gen);
            }
            m_glitchphase = 0;
            m_phase = 0;
            
            std::uniform_real_distribution<float> lendist(0.001,0.08);
            m_glitchlen = lendist(m_gen)*samplerate;
            if (density<0.45)
            {
                float rate = rescale(density,0.0f,0.45f,1.0f/16,1.0f);
                m_nextglitchpos = rate*samplerate;
            } else if (density>0.55)
            {
                float rate = rescale(density,0.55f,1.00f,2.0f,1.0f/16);
                float exprand = -log(random::uniform())/(1.0/rate);
                exprand = clamp(exprand,0.001,5.0f);
                m_nextglitchpos = exprand*samplerate;
            }
        }
        if (density>=0.45 && density<=0.55)
        {
            m_curglitch = GLT_LAST;
            m_phase = 0;
            m_glitchphase = 0;
        }
        float out = in;
        if (m_curglitch==GLT_SILENCE)
        {
            out = 0.0f;
            ++m_glitchphase;
            if (m_glitchphase>=m_glitchlen)
                m_curglitch = GLT_LAST;
        }
        else if (m_curglitch == GLT_KILOSINE)
        {
            out = 0.5f*std::sin(2*3.141592653/samplerate*m_glitchphase*1000.0f);
            ++m_glitchphase;
            if (m_glitchphase>=m_glitchlen)
                m_curglitch = GLT_LAST;
        }
        else if (m_curglitch == GLT_RINGMOD)
        {
            out = in * std::sin(2*3.141592653/samplerate*m_glitchphase*2000.0f);
            ++m_glitchphase;
            if (m_glitchphase>=m_glitchlen)
                m_curglitch = GLT_LAST;
        }
        else if (m_curglitch == GLT_NOISE)
        {
            out = clamp(random::normal(),-0.7,0.7);
            ++m_glitchphase;
            if (m_glitchphase>=m_glitchlen)
                m_curglitch = GLT_LAST;
        }
        else if (m_curglitch == GLT_REPEAT)
        {
            out = m_repeatBuf[m_repeatReadPos];
            ++m_repeatReadPos;
            if (m_repeatReadPos>=m_repeatLen)
                m_repeatReadPos = 0;
            ++m_glitchphase;
            if (m_glitchphase>=m_glitchlen)
                m_curglitch = GLT_LAST;
        }
        ++m_phase;
        return out;
    }
private:
    int m_nextglitchpos = 0;
    int m_phase = 0;
    int m_glitchphase = 0;
    int m_glitchlen = 0;
    float m_curdensity = 0.5f;
    GLITCHES m_curglitch = GLT_LAST;
    std::mt19937 m_gen;
    std::vector<float> m_repeatBuf;
    int m_repeatReadPos = 0;
    int m_repeatWritePos = 0;
    int m_repeatLen = 0;
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
    float process(float in, float insamplerate, float srdiv, float bits, 
        float drive, float dtype, float oversample, float glitchrate)
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
        float glitch = m_glitcher.process(crushed,insamplerate,glitchrate);
        return clamp(glitch,-1.0f,1.0f);
    }
private:
    SampleRateReducer m_reducer;
    dsp::Upsampler<8,2> m_upsampler;
    dsp::Decimator<8,2> m_downsampler;
    GlitchGenerator m_glitcher;
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
        PAR_GLITCHRATE,
        PAR_ATTN_GLITCHRATE,
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
        IN_CV_GLITCHRATE,
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
        configParam(PAR_GLITCHRATE,0.0f,1.0f,0.5,"Glitch rate");
        configParam(PAR_ATTN_GLITCHRATE,-1.0f,1.0f,0.0,"Glitch rate CV");
    }
    
    
    void process(const ProcessArgs& args) override
    {
        float insample = inputs[IN_AUDIO].getVoltageSum()/5.0f;
        
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
        float glitchrate = params[PAR_GLITCHRATE].getValue();
        glitchrate += inputs[IN_CV_GLITCHRATE].getVoltage()*params[PAR_ATTN_GLITCHRATE].getValue()/10.0f;
        glitchrate = clamp(glitchrate,0.0f,1.0f);
        float processed = m_engines[0].process(insample,args.sampleRate,srdiv,bits,drivegain,dtype,osamt,
            glitchrate);
        outputs[OUT_AUDIO].setVoltage(processed*5.0f);
    }
private:
    
    LOFIEngine m_engines[16];
};

class XLOFIWidget : public ModuleWidget
{
public:
    XLOFI* m_lofi = nullptr;
    LOFIEngine m_eng;
    XLOFIWidget(XLOFI* m)
    {
        setModule(m);
        m_lofi = m;
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

        addParam(createParamCentered<RoundBlackKnob>(Vec(15.00, 280), m, XLOFI::PAR_GLITCHRATE));
        addInput(createInputCentered<PJ301MPort>(Vec(45, 280), m, XLOFI::IN_CV_GLITCHRATE));
        addParam(createParamCentered<Trimpot>(Vec(70.00, 280), m, XLOFI::PAR_ATTN_GLITCHRATE));
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
        
        if (m_lofi && 6==3)
        {
            int w = 78*2;
            float srdiv = m_lofi->params[XLOFI::PAR_RATEDIV].getValue();
            srdiv = std::pow(srdiv,2.0f);
            srdiv = 1.0+srdiv*99.0;
            float bitd = m_lofi->params[XLOFI::PAR_BITDIV].getValue();
            float drive = m_lofi->params[XLOFI::PAR_DRIVE].getValue();
            drive = rescale(drive,0.0f,1.0f,-12.0,52.0f);
            drive = dsp::dbToAmplitude(drive);
            float dtype = m_lofi->params[XLOFI::PAR_DISTORTTYPE].getValue();
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x80));
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg,0.0,295.0f);
            nvgLineTo(args.vg,80.0,295.0f);
            nvgStroke(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            nvgBeginPath(args.vg);
            for (int i=0;i<w;++i)
            {
                float s = std::sin(2*3.141592653/w*i*2.0f);
                s = m_eng.process(s,w*2.0f,srdiv,bitd,drive,dtype,0.0f,0.5f);
                float ycor = rescale(s,-1.0f,1.0f,270.0,320.0f);
                float xcor = rescale(i,0,w,0.0,80.0);
                nvgMoveTo(args.vg,xcor,295.0f);
                nvgLineTo(args.vg,xcor,ycor);
                
            }
            nvgStroke(args.vg);
        }
        
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
}
};

Model* modelXLOFI = createModel<XLOFI, XLOFIWidget>("XLOFI");
