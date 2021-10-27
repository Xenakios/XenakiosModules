#include "plugin.hpp"
#include <random>
#include <array>
#include "helperwidgets.h"
#include <set>

class EntropySource
{
public:
    virtual ~EntropySource() {}
    virtual void setSeed(float seed, bool force) = 0;
    virtual void generate(float* dest, int sz) = 0;
    virtual std::string getName() { return "Unknown"; }
};

class MersenneTwister final : public EntropySource
{
public:
    MersenneTwister() {}
    std::string getName() override { return "Mersenne Twister"; }
    void setSeed(float s, bool force) override
    {
        if (s!=m_cur_seed || force)
        {
            m_mt = std::mt19937((int)(s*65536));
            m_cur_seed = s;
        }
    }
    void generate(float* dest, int sz) override
    {
        for (int i=0;i<sz;++i)
            dest[i] = m_ud(m_mt);
    }
    
private:
    std::mt19937 m_mt;
    std::uniform_real_distribution<float> m_ud{0.0f,1.0f};
    float m_cur_seed = 0.0f;
};

class LogisticChaos final : public EntropySource 
{
public:
    LogisticChaos(float r) : m_r(r) 
    {
        m_name = rack::string::f("Chaos %.4f",r);
    }
    std::string getName() override { return m_name; }
    void setSeed(float s, bool force) override
    {
        m_x0 = s;
    }
    void generate(float* dest, int sz) override
    {
        for (int i=0;i<sz;++i)
        {
            dest[i] = m_x0;
            double x1 = m_x0*(1.0-m_x0)*m_r;
            m_x0 = x1;    
        }
    }
    
private:
    double m_x0 = 0.4f;
    double m_r = 3.9f;
    std::string m_name;
};

class LehmerRandom final : public EntropySource
{
public:
    LehmerRandom(unsigned a, unsigned int m) : m_a(a), m_m(m) 
    {
        m_name = "Lehmer "+std::to_string(a)+"/"+std::to_string(m);
    }
    std::string getName() override { return m_name; }
    void setSeed(float s, bool force) override
    {
        m_prev = 1.0f+s*65536.0f;
    }
    void generate(float* dest, int sz) override
    {
        for (int i=0;i<sz;++i)
        {
            unsigned int r = (m_prev*m_a) % m_m;
            dest[i] = rack::math::rescale((float)r,0,m_m-1,0.0f,1.0f);
            m_prev = r;
        }
    }
private:
    unsigned int m_prev = 1;
    unsigned int m_a = 0;
    unsigned int m_m = 0;
    std::string m_name;
};

inline float zero_to_epsilon(float x, float eps = 0.000001)
{
    if (std::abs(x)>0.0f)
        return x;
    return x+eps;
}

inline float ramp_adjustable(float normpos, float startval, float endval, float rampdur)
{
    if (normpos<rampdur)
    {
        return math::rescale(normpos,0.0f,rampdur,startval,endval);
    }
    return endval;
}

template<typename F>
inline float BoxMullerNormal(float mu, float sigma,F&& EntropyFunc)
{
    constexpr double epsilon = std::numeric_limits<double>::epsilon();
    constexpr double two_pi = 2.0 * M_PI;
    double u1, u2;
    int sanity = 0;
    do
    {
        u1 = EntropyFunc();
        u2 = EntropyFunc();
        ++sanity;
        if (sanity>50)
        {
            u1 = 0.5f;
            u2 = 0.4;
            break;
        }
    }
    while (u1 <= epsilon);
    auto mag = sigma * sqrt(-2.0 * log(u1));
    auto z0  = mag * cos(two_pi * u2) + mu;
    //auto z1  = mag * sin(two_pi * u2) + mu;
    return z0;
}

class RandomEngine
{
public:
    RandomEngine()
    {
        m_entsources.emplace_back(new MersenneTwister);
        m_entsources.emplace_back(new LehmerRandom(17,89));
        m_entsources.emplace_back(new LehmerRandom(41,401));
        m_entsources.emplace_back(new LehmerRandom(16807,2147483647));
        m_entsources.emplace_back(new LogisticChaos(3.8));
        m_entsources.emplace_back(new LogisticChaos(3.91));
        m_entsources.emplace_back(new LogisticChaos(3.9997));
        for (int i=0;i<m_entbuf.size();++i)
            m_entbuf[i] = 0.0f;
        for (int i=0;i<m_histogram.size();++i)
            m_histogram[i] = 0;
    }
    void setEntropySource(int s)
    {
        if (s==m_entropySource)
            return;
        if (s>=0 && s < m_entsources.size())
        {
            m_entropySource = s;
            m_entbufpos = 0;
            if (m_seed>=0.0f)
                m_entsources[m_entropySource]->setSeed(m_seed,false);
        }
    }
    int getNumEntropySources()
    {
        return m_entsources.size();
    }
    int getEntroSourceIndex()
    {
        return m_entropySource;
    }
    void setSeed(float s, bool force=false)
    {
        bool doit = false;
        if (s != m_seed && force==false)
            doit = true;
        if (s == m_seed && force==true)
            doit = true;
        if (doit)
        {
            m_entsources[m_entropySource]->setSeed(s,force);
            m_entbufpos = 0;
            m_seed = s;
        }
    }
    void setDistributionType(int t)
    {
        m_distType = rack::math::clamp(t,0,D_LAST-1);
    }
    void setDistributionParameters(float p0, float p1)
    {
        m_distpar0 = rack::math::clamp(p0,-1.0f,1.0f);
        m_distpar1 = rack::math::clamp(p1,0.0f,1.0f);
    }
    void setOutputLimitMode(int m)
    {
        m_clipType = clamp(m,0,2);
    }
    void setFrequency(float Hz)
    {
        m_frequency = Hz;
    }
    inline float getNextEntropy()
    {
        if (m_entbufpos == 0)
            m_entsources[m_entropySource]->generate(m_entbuf.data(),m_entbuf.size());
        float z = m_entbuf[m_entbufpos];
        ++m_entbufpos;
        if (m_entbufpos == m_entbuf.size())
            m_entbufpos = 0;
        return z;
    }
    
    enum Distributions
    {
        D_UNIFORM,
        D_GAUSS,
        D_CAUCHY,
        D_HYPCOS,
        D_LOGISTIC,
        D_UNIEXP,
        D_BIEXP,
        D_ARCSINE,
        D_LINEAR,
        D_TRIANGULAR,
        D_LAST
    };
    std::string getDistributionName(int index)
    {
        if (index == -1)
            index = m_distType;
        if (index == D_UNIFORM) return "Uniform";
        else if (index == D_GAUSS) return "Gaussian";
        else if (index == D_CAUCHY) return "Cauchy";
        else if (index == D_HYPCOS) return "Hypcos";
        else if (index == D_LOGISTIC) return "Logistic";
        else if (index == D_UNIEXP) return "Unilat Exp";
        else if (index == D_BIEXP) return "Bilat Exp";
        else if (index == D_ARCSINE) return "ArcSine";
        else if (index == D_LINEAR) return "Linear";
        else if (index == D_TRIANGULAR) return "Triangular";
        return "Unknown";
    }
    std::string getEntropySourceName(int index)
    {
        if (index == -1)
            index = m_entropySource;
        if (index>=0 && index<m_entsources.size())
        {
            return m_entsources[m_entropySource]->getName();
        }
        return "Unknown";
    }
    // return values nominally in the -5 to 5 range from here
    inline float getNextShaped()
    {
        float z = getNextEntropy();
        if (m_distType == D_UNIFORM) // uniform
            return rack::math::rescale(z,0.0f,1.0f,-5.0f,5.0f);
        else if (m_distType == D_GAUSS) // Gauss
        {
            float nmean = rack::math::rescale(m_distpar0,-1.0f,1.0f,-5.0f,5.0f);
            float nsigma = rack::math::rescale(m_distpar1,0.0f,1.0f, 0.0f,3.0f);
            return BoxMullerNormal(nmean,nsigma,[this](){ return getNextEntropy(); });
        }
        else if (m_distType == D_CAUCHY) // Cauchy
        {
            float cmean = rack::math::rescale(m_distpar0,-1.0f,1.0f,-5.0f,5.0f);
            // spread parameter shaped with pow because Cauchy behaves more interestingly with lower spread values
            float csigma = rack::math::rescale(std::pow(m_distpar1,3.0f),0.0f,1.0f,0.0f,3.0f);
            // the uniform variable can't be one or zero for the Cauchy generation!
            // that might happen quite often with the purposely bad entropy sources used...
            z = clamp(z,0.000001f,0.9999999f);
            return cmean+csigma*std::tan(M_PI*(z-0.5f));
        }
        else if (m_distType == D_UNIEXP) // Unilateral Exponential with shift
        {
            float eshift = rack::math::rescale(m_distpar0,-1.0f,1.0f,-5.0,5.0f);
            float espread = rack::math::rescale(m_distpar1,0.0f,1.0f,0.0001,5.0f);
            return eshift+((-std::log(z)/espread));
        }
        else if (m_distType == D_ARCSINE) // Arc Sine with shift and spread
        {
            float eshift = rack::math::rescale(m_distpar0,-1.0f,1.0f,-5.0,5.0f);
            float espread = rack::math::rescale(m_distpar1,0.0f,1.0f,0.0,5.0f);
            return rack::math::rescale( ( (1.0f-std::sin(M_PI*(z-0.5f)) ) / 2.0f) , 0.0f, 1.0f, eshift-espread, eshift+espread);
        }
            
        else if (m_distType == D_BIEXP) // Bilateral exponential
        {
            float eshift = rack::math::rescale(m_distpar0,-1.0f,1.0f,-5.0,5.0f);
            float espread = rack::math::rescale(m_distpar1,0.0f,1.0f,0.0001,5.0f);
            float temp_e = ((-std::log(getNextEntropy())/espread));
            if (z<0.5f)
                return eshift - temp_e;
            else return eshift + temp_e;
        }
        else if (m_distType == D_LINEAR) // Linear with shift and spread
        {
            float eshift = rack::math::rescale(m_distpar0,-1.0f,1.0f,-5.0,5.0f);
            float espread = rack::math::rescale(m_distpar1,0.0f,1.0f,0.0f,5.0f);
            return eshift + espread * (1.0f - std::sqrt(z));
        }
            
        else if (m_distType == D_TRIANGULAR)
        {
            float temp_t = (1.0f - std::sqrt(z));
            float eshift = rack::math::rescale(m_distpar0,-1.0f,1.0f,-5.0,5.0f);
            float espread = rack::math::rescale(m_distpar1,0.0f,1.0f,0.0f,5.0f);
            temp_t = espread * temp_t;
            if (getNextEntropy()<0.5f)
                temp_t = -temp_t;
            return eshift + temp_t;
        }
        else if (m_distType == D_HYPCOS)
        {
            float eshift = rack::math::rescale(m_distpar0,-1.0f,1.0f,-5.0,5.0f);
            float espread = rack::math::rescale(m_distpar1,0.0f,1.0f,0.0f,3.0f);
            return eshift+espread*std::log(std::tan((M_PI*z)/2.0f));
        }
            
        else if (m_distType == D_LOGISTIC)
        {
            float eshift = rack::math::rescale(m_distpar0,-1.0f,1.0f,-5.0,5.0f);
            float espread = rack::math::rescale(m_distpar1,0.0f,1.0f,0.05f,10.0f);
            return (eshift-std::log(1.0f/z-1.0f))/espread;
        }
            
        return z;
    }
    inline float quantize(float v, int numsteps)
    {
        float nsteps = numsteps/10.0f;
        return std::round(v*nsteps)/nsteps;
    }
    std::array<int,1024> m_histogram;
    bool m_calcHistoGram = false;
    float getNext(float deltatime)
    {
        double deltaPhase = std::fmin(m_frequency*deltatime,0.5);
        m_phase += deltaPhase;
        if (m_phase>=1.0)
        {
            m_start_val = m_end_val;
            m_end_val = getNextShaped();
            m_phase -= 1.0;
        }
        float quanstart = quantize(m_start_val,m_quantSteps);
        float quanend = quantize(m_end_val,m_quantSteps);
        if (m_calcHistoGram)
        {
            int hindex = rack::math::rescale(quanstart,-5.0f,5.0f,0.0f,(float)m_histogram.size()-1.0f);
            if (hindex>=0 && hindex<m_histogram.size())
                ++m_histogram[hindex];
        }
        float out = ramp_adjustable(m_phase,quanstart,quanend,m_smoothpar0);
        if (m_clipType == 0)
            out = rack::math::clamp(out,m_min_val,m_max_val);
        else if (m_clipType == 1)
            out = reflect_value_safe(m_min_val,out,m_max_val);
        else out = wrap_value_safe(m_min_val,out,m_max_val);
        return out;
    }
    void setLimits(float lowlim, float highlim)
    {
        lowlim = rack::math::clamp(lowlim,-5.0f,5.0f);
        highlim = rack::math::clamp(highlim,-5.0f,5.0f);
        if (lowlim>highlim)
            std::swap(lowlim,highlim);
        m_min_val = lowlim;
        m_max_val = highlim;
    }
    void setSmoothingParameters(float s0, float s1)
    {
        m_smoothpar0 = s0;
        m_smoothpar1 = s1;
    }
    void setQuantizeSteps(int s)
    {
        m_quantSteps = s;
    }
    void reset()
    {
        m_phase = 0.0;
        setSeed(m_seed,true);
    }
private:
    double m_phase = 0.0;
    double m_frequency = 8.0;
    float m_start_val = 0.0f;
    float m_end_val = 0.0f;
    int m_entropySource = 0;
    int m_distType = 0;
    int m_clipType = 0;
    float m_distpar0 = 0.0f;
    float m_distpar1 = 1.0f;
    float m_smoothpar0 = 1.0f;
    float m_smoothpar1 = 0.0f;
    std::vector<std::unique_ptr<EntropySource>> m_entsources;
    // because of the virtual method, the entropy sources generate a buffer of entropy at once...
    std::array<float,64> m_entbuf;
    int m_entbufpos = 0;
    float m_min_val = -5.0f;
    float m_max_val = 5.0f;
    float m_seed = -1.0f;
    int m_quantSteps = 65536;
    std::normal_distribution<float> m_dist_normal{0.0,1.0};
};

class XRandomModule : public Module
{
public:
    enum PARAMS
    {
        PAR_RATE,
        PAR_ENTROPY_SOURCE,
        PAR_ENTROPY_SEED,
        PAR_DIST_TYPE,
        PAR_DIST_PAR0,
        PAR_DIST_PAR1,
        PAR_LIMIT_TYPE,
        PAR_LIMIT_MIN,
        PAR_LIMIT_MAX,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_RESET,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_MAIN,
        OUT_LAST
    };
    XRandomModule()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_RATE,-8.0f,12.0f,1.0f,"Rate", " Hz",2,1);
        configParam(PAR_ENTROPY_SOURCE,0,m_eng.getNumEntropySources()-1,0.0f,"Entropy source");
        configParam(PAR_ENTROPY_SEED,0.0f,1.0f,0.0f,"Entropy seed");
        configParam(PAR_DIST_TYPE,0.0f,RandomEngine::D_LAST-1,0.0f,"Distribution type");
        configParam(PAR_DIST_PAR0,-1.0f,1.0f,0.0f,"Distribution par 0");
        configParam(PAR_DIST_PAR1,0.0f,1.0f,0.0f,"Distribution par 1");
        configParam(PAR_LIMIT_TYPE,0.0f,2.0f,0.0f,"Limit type");
        configParam(PAR_LIMIT_MIN,-5.0f,5.0f,-5.0f,"Min Limit");
        configParam(PAR_LIMIT_MAX,-5.0f,5.0f,5.0f,"Max Limit");
    }
    void process(const ProcessArgs& args) override
    {
        float pitch = params[PAR_RATE].getValue()*12.0f;
        float rate = std::pow(2.0f,1.0f/12*pitch);
        m_eng.setFrequency(rate);
        int esource = params[PAR_ENTROPY_SOURCE].getValue();
        m_eng.setEntropySource(esource);
        float dpar0 = params[PAR_DIST_PAR0].getValue();
        float dpar1 = params[PAR_DIST_PAR1].getValue();
        m_eng.setDistributionParameters(dpar0,dpar1);
        int dtype = params[PAR_DIST_TYPE].getValue();
        m_eng.setDistributionType(dtype);
        int lmode = params[PAR_LIMIT_TYPE].getValue();
        m_eng.setOutputLimitMode(lmode);
        float lim_min = params[PAR_LIMIT_MIN].getValue();
        float lim_max = params[PAR_LIMIT_MAX].getValue();
        m_eng.setLimits(lim_min,lim_max);
        float eseed = params[PAR_ENTROPY_SEED].getValue();
        m_eng.setSeed(eseed);
        if (m_reset_trig.process(inputs[IN_RESET].getVoltage()))
            m_eng.reset();
        outputs[OUT_MAIN].setVoltage(m_eng.getNext(args.sampleTime),0);
    }
    RandomEngine m_eng;
    dsp::SchmittTrigger m_reset_trig;
private:

};

class XRandomModuleWidget : public ModuleWidget
{
public:
    XRandomModuleWidget(XRandomModule *m)
    {
        setModule(m);
        box.size.x = RACK_GRID_WIDTH * 24;
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "X-RANDOM",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        auto port = new PortWithBackGround(m,this, XRandomModule::OUT_MAIN ,1,30,"OUT",true);
        new PortWithBackGround(m,this, XRandomModule::IN_RESET ,62,30,"RESET",false);
        std::set<int> snappars{XRandomModule::PAR_ENTROPY_SOURCE,XRandomModule::PAR_DIST_TYPE,XRandomModule::PAR_LIMIT_TYPE};
        if (m)
        {
            for (int i=0;i<m->paramQuantities.size();++i)
            {
                int xpos = i % 4;
                int ypos = i / 4;
                float xcor = 5.0f+84.0f*xpos;
                float ycor = 75.0f+48.0f*ypos;
                auto name = m->paramQuantities[i]->label;
                bool snap = false;
                if (snappars.count(i))
                    snap = true;
                addChild(new KnobInAttnWidget(this,name,i,-1,-1,xcor,ycor,snap));
            }
        }
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
        
        XRandomModule* m = dynamic_cast<XRandomModule*>(module);
        if (m)
        {
            auto entrname = m->m_eng.getEntropySourceName(-1);
            auto distname = m->m_eng.getDistributionName(-1);
            auto thetext = entrname+" -> "+distname;
            nvgFontSize(args.vg, 20);
            nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            nvgText(args.vg,1.0f,h-20.0f,thetext.c_str(),nullptr);
        }
        
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelXRandom = createModel<XRandomModule, XRandomModuleWidget>("XRandom");
