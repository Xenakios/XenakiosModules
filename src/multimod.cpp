#include "plugin.hpp"

inline float adjustable_triangle(float in, float peakpos)
{
    in = fmod(in,1.0f);
    if (in<peakpos)
        return rescale(in,0.0f,peakpos,0.0f,1.0f);
    return rescale(in,peakpos,1.0f,1.0f,0.0f);
}

const int msnumtables = 7;
const int mstablesize = 1024;

class ModulationShaper
{
public:
    ModulationShaper()
    {
        for (int i=0;i<mstablesize;++i)
        {
            float norm = 1.0/(mstablesize-1)*i;
            m_tables[0][i] = std::pow(norm,5.0f);
            m_tables[1][i] = std::pow(norm,2.0f);
            m_tables[2][i] = norm;
            m_tables[3][i] = 0.5-0.5*std::sin(3.141592653*(0.5+norm));
            m_tables[4][i] = 1.0f-std::pow(1.0f-norm,5.0f);
            m_tables[5][i] = clamp(norm+random::normal()*0.1,0.0,1.0f);
            m_tables[6][i] = std::round(norm*7)/7;
        }
        // fill guard point by repeating value
        for (int i=0;i<msnumtables;++i)
            m_tables[i][mstablesize] = m_tables[i][mstablesize-1];
        // fill guard table by repeating table
        for (int i=0;i<mstablesize;++i)
            m_tables[msnumtables][i]=m_tables[msnumtables-1][i];
    }
    float process(float morph, float input)
    {
        float z = morph*(msnumtables-1);
        int xindex0 = morph*(msnumtables-1);
        int xindex1 = xindex0+1;
        int yindex0 = input*(mstablesize-1);
        int yindex1 = yindex0+1;
        float x_a0 = m_tables[xindex0][yindex0];
        float x_a1 = m_tables[xindex0][yindex1];
        float x_b0 = m_tables[xindex1][yindex0];
        float x_b1 = m_tables[xindex1][yindex0];
        float xfrac = (input*mstablesize)-yindex0;
        float x_interp0 = x_a0+(x_a1-x_a0) * xfrac;
        float x_interp1 = x_b0+(x_b1-x_b0) * xfrac;
        float yfrac=z-(int)z;
        return x_interp0+(x_interp1-x_interp0) * yfrac;
        
    }
private:
    
    float m_tables[msnumtables+1][mstablesize+1];

};

ModulationShaper g_shaper;

class XLFO
{
public:
    XLFO() 
    {
        m_offsetSmoother.setAmount(0.99);
    }
    float process(float masterphase)
    {
        float smoothedOffs = m_offsetSmoother.process(m_offset);
        float out = adjustable_triangle((smoothedOffs+masterphase)*m_freqmultip,m_slope);
        /*
        if (m_shape<0.5f)
            out = std::pow(out,rescale(m_shape,0.0f,0.5f,8.0f,1.0f));
        else 
            out = 1.0f-std::pow(1.0f-out,rescale(m_shape,0.5f,1.0f,1.0f,8.0f));
        */
        out = g_shaper.process(m_shape,out);        

        return -1.0+2.0f*out;
    }
    void setFrequencyMultiplier(float r)
    {
        m_freqmultip = r;
    }
    void setOffset(float offs)
    {
        m_offset = offs;
    }
    void setSlope(float s)
    {
        m_slope = s;
    }
    void setShape(float s)
    {
        m_shape = s;
    }
private:
    // double m_phase = 0.0f;
    double m_freqmultip = 1.0f;
    double m_offset = 0.0f;
    float m_slope = 0.5f;
    float m_shape = 0.5f;
    OnePoleFilter m_offsetSmoother;
};

class XMultiMod : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_RATE,
        PAR_NUMOUTPUTS,
        PAR_OFFSET,
        PAR_FREQMULTIP,
        PAR_SLOPE,
        PAR_SHAPE,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_MODOUT,
        OUT_LAST
    };
    XMultiMod()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_RATE,0.1f,32.0f,1.0f,"Base rate");
        configParam(PAR_NUMOUTPUTS,1,16,4.0,"Number of outputs");
        configParam(PAR_OFFSET,0.0f,1.0f,0.0f,"Outputs phase offset");
        configParam(PAR_FREQMULTIP,-1.0f,1.0f,0.0f,"Outputs frequency multiplication");
        configParam(PAR_SLOPE,0.0f,1.0f,0.5f,"Slope");
        configParam(PAR_SHAPE,0.0f,1.0f,0.5f,"Shape");
        
    }
    void updateLFORateMultipliers(int numoutputs, float masterMultip)
    {
        for (int i=0;i<numoutputs;++i)
        {
            if (masterMultip>=0.0f)
            {
                int f = 1.0+i*masterMultip;
                m_lfos[i].setFrequencyMultiplier(f);
            } else
            {
                int f = 1.0+i*(1.0f+masterMultip);
                m_lfos[i].setFrequencyMultiplier(1.0f/f);
            }
            
        }
    }
    void process(const ProcessArgs& args) override
    {
        float rate = params[PAR_RATE].getValue();
        float offset = params[PAR_OFFSET].getValue();
        float fmult = params[PAR_FREQMULTIP].getValue();
        float slope = params[PAR_SLOPE].getValue();
        float shape = params[PAR_SHAPE].getValue();
        int numoutputs = params[PAR_NUMOUTPUTS].getValue();
        numoutputs = clamp(numoutputs,1,16);
        outputs[OUT_MODOUT].setChannels(numoutputs);
        if (m_phase == 0.0)
            updateLFORateMultipliers(numoutputs,fmult);
        for (int i=0;i<numoutputs;++i)
        {
            m_lfos[i].setSlope(slope);
            m_lfos[i].setShape(shape);
            m_lfos[i].setOffset(offset*(1.0/numoutputs*i));
            float out = m_lfos[i].process(m_phase);
            outputs[OUT_MODOUT].setVoltage(5.0f*out,i);
        }
        m_phase+=args.sampleTime*rate;
        if (m_phase>=1.0)
        {
            m_phase-=1.0;
            updateLFORateMultipliers(numoutputs,fmult);
        }
    }
private:
    XLFO m_lfos[16];
    double m_phase = 0.0;
};

class XMultiModWidget : public ModuleWidget
{
public:
    std::shared_ptr<rack::Font> m_font;
    XMultiModWidget(XMultiMod* m)
    {
        setModule(m);
        box.size.x = 120;
        addOutput(createOutput<PJ301MPort>(Vec(3, 30), m, XMultiMod::OUT_MODOUT));
        RoundBlackKnob* knob = nullptr;
        addParam(createParam<RoundBlackKnob>(Vec(3, 60), m, XMultiMod::PAR_RATE));
        addParam(createParam<RoundBlackKnob>(Vec(43, 60), m, XMultiMod::PAR_OFFSET));
        addParam(createParam<RoundBlackKnob>(Vec(83, 60), m, XMultiMod::PAR_FREQMULTIP));
        addParam(createParam<RoundBlackKnob>(Vec(43, 95), m, XMultiMod::PAR_SLOPE));
        addParam(createParam<RoundBlackKnob>(Vec(83, 95), m, XMultiMod::PAR_SHAPE));
        addParam(knob = createParam<RoundBlackKnob>(Vec(3, 95), m, XMultiMod::PAR_NUMOUTPUTS));
        knob->snap = true;
    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x50, 0x50, 0x50, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelXMultiMod = createModel<XMultiMod, XMultiModWidget>("XMultiMod");
