#include "plugin.hpp"
#include "helperwidgets.h"
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
            if (m_curglitch == GLT_RINGMOD)
            {
                std::uniform_real_distribution<float> fdist(20.0f,7000.0);
                m_ringmodfreq = fdist(m_gen);
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
            out = in * std::sin(2*3.141592653/samplerate*m_glitchphase*m_ringmodfreq);
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
    bool glitchActive() { return m_curglitch!=GLT_LAST; }
private:
    int m_nextglitchpos = 0;
    int m_phase = 0;
    int m_glitchphase = 0;
    int m_glitchlen = 0;
    float m_ringmodfreq = 1.0f;
    GLITCHES m_curglitch = GLT_LAST;
    std::mt19937 m_gen;
    std::vector<float> m_repeatBuf;
    int m_repeatReadPos = 0;
    int m_repeatWritePos = 0;
    int m_repeatLen = 0;
};

inline float sin_dist(float in)
{
    return std::sin(3.141592653*2*in);
}

inline float sign(float in)
{
    if (in<0.0f)
        return -1.0f;
    return 1.0f;
}

inline float sym_reflect(float in)
{
    return sign(in)*2.0f*std::fabs(in/2.0f-std::round(in/2.0f));
}

inline float sym_wrap(float in)
{
    return 2.0f*(in/2.0f-std::round(in/2.0f));
}

struct RandShaper
{
    std::vector<float> m_shapefunc;
    RandShaper()
    {
        m_shapefunc.resize(4096);
        std::mt19937 gen(912477);
        std::uniform_int_distribution<int> dist(-30,30);
        for (int i=0;i<m_shapefunc.size();++i)
            m_shapefunc[i]=rescale(dist(gen),-30,30,-1.0f,1.0f);
    }
    inline float process(float in)
    {
        in = clamp(in,-32.0f,32.0f)+32.0f;
        int index = in * (m_shapefunc.size()/64-1);
        index = clamp(index,0,m_shapefunc.size()-1);
        return m_shapefunc[index];    
    }
};

inline float distort(float in, float th, float type, RandShaper& rshaper)
{
    int index0 = std::floor(type);
    int index1 = index0+1;
    float frac = type-index0;
    float distsamples[7];
    if (index0 == 0)
    {
        distsamples[0] = soft_clip(in);
        distsamples[1] = clamp(in,-th,th);
    }
    else if (index0 == 1)
    {
        distsamples[1] = clamp(in,-th,th);
        distsamples[2] = sym_reflect(in);
    }
    else if (index0 == 2)
    {
        distsamples[2] = sym_reflect(in);
        distsamples[3] = sym_wrap(in);
    }
    else if (index0 == 3)
    {
        distsamples[3] = sym_wrap(in);
        //distsamples[3] = wrap_value(-th,in,th);
        if (frac>0.0f) 
            distsamples[4] = sin_dist(in);
        else
            distsamples[4] = distsamples[3];
    } 
    else if (index0 == 4)
    {
        distsamples[4] = sin_dist(in);
        if (frac>0.0f)
            distsamples[5] = rshaper.process(in);
        else
            distsamples[5] = distsamples[4];
    }
    else if (index0 == 5)
    {
        distsamples[5] = rshaper.process(in);
        distsamples[6] = distsamples[5];
    }
    
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
        float drive, float dtype, float oversample, float glitchrate, float dcoffs)
    {
        in+=dcoffs;
        float driven = drive*in;
        float oversampledriven = 0.0f;
        if (oversample>0.0f) // only oversample when oversampled signal is going to be mixed in
        {
            float osarr[8];
            m_upsampler.process(driven,osarr);
            for (int i=0;i<8;++i)
                osarr[i] = distort(osarr[i],1.0f,dtype,m_randshaper);
            oversampledriven = 2.0f*m_downsampler.process(osarr);
        }
        
        driven = distort(driven,1.0f,dtype,m_randshaper);
        float drivemix = (1.0f-oversample) * driven + oversample * oversampledriven;
        m_reducer.setRates(insamplerate,insamplerate/srdiv);
        float reduced = m_reducer.process(drivemix);
        bits = getBitDepthFromNormalized(bits);
        float bitlevels = std::pow(2.0f,bits)/2.0f;
        float crushed = reduced; //*32767.0;
        crushed = std::round(crushed*bitlevels)/bitlevels;
        //crushed /= 32767.0;
        float glitch = m_glitcher.process(crushed,insamplerate,glitchrate);
        // glitch-=dcoffs;
        return clamp(glitch,-1.0f,1.0f);
    }
    bool glitchActive() { return m_glitcher.glitchActive(); }
    RandShaper m_randshaper;
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
        OUT_GLITCH_TRIG,
        OUT_SIGNALCOMPLEXITY,
        LAST_OUTPUT
    };
    XLOFI()
    {
        config(PAR_LAST,LAST_INPUT,LAST_OUTPUT);
        configParam(PAR_RATEDIV,0.0,1.0,0.0,"Sample rate reduction");
        configParam(PAR_BITDIV,0.0,1.0,1.0,"Bit depth");
        configParam(PAR_DRIVE,0.0,1.0,0.15,"Drive");
        configParam(PAR_DISTORTTYPE,0,5,0,"Distortion type");
        configParam(PAR_ATTN_RATEDIV,-1.0f,1.0f,0.0,"Sample rate reduction CV");
        configParam(PAR_ATTN_BITDIV,-1.0f,1.0f,0.0,"Bit depth CV");
        configParam(PAR_ATTN_DRIVE,-1.0f,1.0f,0.0,"Drive CV");
        configParam(PAR_ATTN_DISTYPE,-1.0f,1.0f,0.0,"Distortion type CV");
        configParam(PAR_OVERSAMPLE,0.0f,1.0f,0.0,"Distortion oversampling mix");
        configParam(PAR_ATTN_OVERSAMPLE,-1.0f,1.0f,0.0,"Distortion oversampling mix CV");
        configParam(PAR_GLITCHRATE,0.0f,1.0f,0.5,"Glitch rate");
        configParam(PAR_ATTN_GLITCHRATE,-1.0f,1.0f,0.0,"Glitch rate CV");
        m_fftbuffer.resize(4096);
        m_mag_array.resize(4096);
        m_smoother.setAmount(0.9995);
    }
    
    float complexity = 0.0f;
    int m_numPeaks = 0;
    void process(const ProcessArgs& args) override
    {
        float insample = inputs[IN_AUDIO].getVoltageSum()/5.0f;
        if (outputs[OUT_SIGNALCOMPLEXITY].isConnected())
        {
            m_fftbuffer[m_fftcounter] = insample;
            ++m_fftcounter;
            if (m_fftcounter>=m_fft.length)
            {
                m_fftcounter = 0;
                dsp::hannWindow(m_fftbuffer.data(),m_fft.length);
                m_fft.rfft(m_fftbuffer.data(),m_fftbuffer.data());
                m_fft.scale(m_fftbuffer.data());
                int numpeaks = 0;
                int maglen = m_fft.length/2;
                for (int i=0;i<maglen;++i)
                {
                    float re = m_fftbuffer[i*2];
                    float im = m_fftbuffer[i*2+1];
                    float mag = (sqrt(re*re+im*im));
                    if (mag>0.001)
                        m_mag_array[i]=mag;
                    else m_mag_array[i]=0.0f;
                }
                for (int i=1;i<maglen-1;++i)
                {
                    float s0 = m_mag_array[i-1];
                    float s1 = m_mag_array[i];
                    float s2 = m_mag_array[i+1];
                    if (s1>s0 && s1>s2)
                        ++numpeaks;
                }
                m_numPeaks = numpeaks;
                complexity = rescale((float)numpeaks,0,300,0.0f,1.0f);
                complexity = clamp(complexity,0.0f,1.0f);
                complexity = 1.0f-std::pow(1.0f-complexity,2.0f);
                
            }
            float smoothed = m_smoother.process(complexity);
            outputs[OUT_SIGNALCOMPLEXITY].setVoltage(smoothed*10.0f);
        }
        if (!outputs[OUT_AUDIO].isConnected())
            return;
        float drivegain = params[PAR_DRIVE].getValue();
        drivegain += inputs[IN_CV_DRIVE].getVoltage()*params[PAR_ATTN_DRIVE].getValue()/10.0f;
        drivegain = clamp(drivegain,0.0f,1.0f);
        drivegain = rescale(drivegain,0.0f,1.0f,-12.0,52.0f);
        drivegain = dsp::dbToAmplitude(drivegain);
        float dtype = params[PAR_DISTORTTYPE].getValue();
        dtype += inputs[IN_CV_DISTTYPE].getVoltage()*params[PAR_ATTN_DISTYPE].getValue()/3.0f;
        dtype = clamp(dtype,0.0f,5.0f);
        float srdiv = params[PAR_RATEDIV].getValue(); 
        srdiv += inputs[IN_CV_RATEDIV].getVoltage()*params[PAR_ATTN_RATEDIV].getValue()/10.0f;
        srdiv = clamp(srdiv,0.0f,1.0f);
        srdiv = std::pow(srdiv,2.0f);
        srdiv = 1.0+srdiv*99.0;
        float bits = params[PAR_BITDIV].getValue();
        float dcoffs = 0.0f;
        if (inputs[IN_CV_BITDIV].isConnected())
        {
            bits += inputs[IN_CV_BITDIV].getVoltage()*params[PAR_ATTN_BITDIV].getValue()/10.0f;
            bits = clamp(bits,0.0f,1.0f);
        }
        
        float osamt = params[PAR_OVERSAMPLE].getValue();
        if (inputs[IN_CV_OVERSAMPLE].isConnected())
        {
            osamt += inputs[IN_CV_OVERSAMPLE].getVoltage()*params[PAR_ATTN_OVERSAMPLE].getValue()/10.0f;
            osamt = clamp(osamt,0.0f,1.0f);
        } else
        {
            dcoffs = 0.5f*params[PAR_ATTN_OVERSAMPLE].getValue();
        }

        
        float glitchrate = params[PAR_GLITCHRATE].getValue();
        glitchrate += inputs[IN_CV_GLITCHRATE].getVoltage()*params[PAR_ATTN_GLITCHRATE].getValue()/10.0f;
        glitchrate = clamp(glitchrate,0.0f,1.0f);
        float processed = m_engines[0].process(insample,args.sampleRate,srdiv,bits,drivegain,dtype,osamt,
            glitchrate,dcoffs);
        outputs[OUT_AUDIO].setVoltage(processed*5.0f);
        if (outputs[OUT_GLITCH_TRIG].isConnected())
        {
            if (m_engines[0].glitchActive())
                outputs[OUT_GLITCH_TRIG].setVoltage(5.0f);
            else 
                outputs[OUT_GLITCH_TRIG].setVoltage(0.0f);
        }
    }
    std::vector<float> m_mag_array;
    dsp::RealFFT m_fft{2048};
private:
    
    LOFIEngine m_engines[16];
    
    std::vector<float> m_fftbuffer;
    
    int m_fftcounter = 0;
    OnePoleFilter m_smoother;
};

extern std::shared_ptr<Font> g_font;

struct LabelEntry
{
    LabelEntry() {}
    LabelEntry(std::string t, float x, float y) : text(t), xpos(x), ypos(y) {}
    std::string text;
    float xpos = 0.0f;
    float ypos = 0.0f;
};

class XLOFIWidget : public ModuleWidget
{
public:
    XLOFI* m_lofi = nullptr;
    LOFIEngine m_eng;
    std::shared_ptr<rack::Font> m_font;
    XLOFIWidget(XLOFI* m)
    {
        setModule(m);
        m_lofi = m;
        box.size.x = 87;
        auto font = APP->window->loadFont(asset::plugin(pluginInstance, "res/Nunito-Bold.ttf"));
        m_font = font;
        PortWithBackGround* port = nullptr;
        port = new PortWithBackGround(m,this,XLOFI::IN_AUDIO,3,14,"AUDIO IN",false);
        float xoffs = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,XLOFI::OUT_AUDIO,xoffs,14,"AUDIO OUT",true);
        float yoffs = port->box.getBottom()+2;
        xoffs = 3;
        port = new PortWithBackGround(m,this,XLOFI::OUT_SIGNALCOMPLEXITY,xoffs,yoffs,"ANALYSIS OUT",true);
        xoffs = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,XLOFI::OUT_GLITCH_TRIG,xoffs,yoffs,"GLITCH ACTIVE",true);
        

        float ydiff = 45.0f;
        yoffs = port->box.pos.y+port->box.size.y+3;
        xoffs = 3.0f;
        addChild(new KnobInAttnWidget(this,"SAMPLERATE DIV",
            XLOFI::PAR_RATEDIV,XLOFI::IN_CV_RATEDIV,XLOFI::PAR_ATTN_RATEDIV,xoffs,yoffs));
        
        yoffs+=ydiff;
        addChild(new KnobInAttnWidget(this,"BIT DEPTH",
            XLOFI::PAR_BITDIV,XLOFI::IN_CV_BITDIV,XLOFI::PAR_ATTN_BITDIV,xoffs,yoffs));
        yoffs+=ydiff;
        addChild(new KnobInAttnWidget(this,"INPUT DRIVE",
            XLOFI::PAR_DRIVE,XLOFI::IN_CV_DRIVE,XLOFI::PAR_ATTN_DRIVE,xoffs,yoffs));
        yoffs+=ydiff;
        addChild(new KnobInAttnWidget(this,"DISTORTION TYPE",
            XLOFI::PAR_DISTORTTYPE,XLOFI::IN_CV_DISTTYPE,XLOFI::PAR_ATTN_DISTYPE,xoffs,yoffs));
        yoffs+=ydiff;
        addChild(new KnobInAttnWidget(this,"DIST OS MIX",
            XLOFI::PAR_OVERSAMPLE,XLOFI::IN_CV_OVERSAMPLE,XLOFI::PAR_ATTN_OVERSAMPLE,xoffs,yoffs));
        yoffs+=ydiff;
        addChild(new KnobInAttnWidget(this,"GLITCH RATE",
            XLOFI::PAR_GLITCHRATE,XLOFI::IN_CV_GLITCHRATE,XLOFI::PAR_ATTN_GLITCHRATE,xoffs,yoffs));
        
        

        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "LOFI",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        addChild(new LabelWidget({{1,189},{box.size.x-4.0f,1}}, "Xenakios",10,nvgRGB(255,255,255),LabelWidget::J_RIGHT));
    }
    int negCount = 0;
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x50, 0x50, 0x50, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);
        if (m_lofi && 6==7)
        {
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            nvgBeginPath(args.vg);
            int fftlen = m_lofi->m_fft.length/2;
            for (int i=0;i<fftlen;++i)
            {
                float s = m_lofi->m_mag_array[i]*4.0f;
                if (s<0.0f)
                    ++negCount;
                float ycor = rescale(s,-1.0f,1.0f,400.0,330.0f);
                float xcor = rescale(i,0,fftlen,1.0,129.0);
                if (i == 0)
                    nvgMoveTo(args.vg,xcor,375.0f);
                nvgLineTo(args.vg,xcor,ycor);
                
            }
            nvgStroke(args.vg);
            char buf[100];
            sprintf(buf,"%d %d",m_lofi->m_numPeaks, negCount);
            nvgFontSize(args.vg, 15);
            nvgFontFaceId(args.vg, m_font->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            
            nvgText(args.vg, 90 , 375, buf, NULL);
            
        }
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
                s = m_eng.process(s,w*2.0f,srdiv,bitd,drive,dtype,0.0f,0.5f,0.0f);
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
