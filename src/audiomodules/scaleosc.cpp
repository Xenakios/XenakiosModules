// #include "plugin.hpp"
// #include "helperwidgets.h"
#include <Vector.hpp>
#include <functions.hpp>
#include <dsp/common.hpp>
#include "wtosc.h"
#include <jansson.h>
#include <array>
#include "jcdp_envelope.h"
#include "Tunings.h"
#include <string.hpp>
// #include <osdialog.h>
#include <thread>
#include "scalehelpers.h"
// Taken from Surge synth src/common/dsp/QuadFilterWaveshapers.cpp

template <int pts> struct FolderADAA
{
    FolderADAA(std::initializer_list<float> xi, std::initializer_list<float> yi)
    {
        auto xiv = xi.begin();
        auto yiv = yi.begin();
        for (int i = 0; i < pts; ++i)
        {
            xs[i] = *xiv;
            ys[i] = *yiv;

            xiv++;
            yiv++;
        }

        slopes[pts - 1] = 0;
        dxs[pts - 1] = 0;

        intercepts[0] = -xs[0] * ys[0];
        for (int i = 0; i < pts - 1; ++i)
        {
            dxs[i] = xs[i + 1] - xs[i];
            slopes[i] = (ys[i + 1] - ys[i]) / dxs[i];
            auto vLeft = slopes[i] * dxs[i] * dxs[i] / 2 + ys[i] * xs[i + 1] + intercepts[i];
            auto vRight = ys[i + 1] * xs[i + 1];
            intercepts[i + 1] = -vRight + vLeft;
        }

        for (int i = 0; i < pts; ++i)
        {
            xS[i] = _mm_set1_ps(xs[i]);
            yS[i] = _mm_set1_ps(ys[i]);
            mS[i] = _mm_set1_ps(slopes[i]);
            cS[i] = _mm_set1_ps(intercepts[i]);
        }
    }

    inline void evaluate(__m128 x, __m128 &f, __m128 &adf)
    {
        static const auto p05 = _mm_set1_ps(0.5f);
        __m128 rangeMask[pts - 1], val[pts - 1], adVal[pts - 1];

        for (int i = 0; i < pts - 1; ++i)
        {
            rangeMask[i] = _mm_and_ps(_mm_cmpge_ps(x, xS[i]), _mm_cmplt_ps(x, xS[i + 1]));
            auto ox = _mm_sub_ps(x, xS[i]);
            val[i] = _mm_add_ps(_mm_mul_ps(mS[i], ox), yS[i]);
            adVal[i] = _mm_add_ps(_mm_mul_ps(_mm_mul_ps(ox, ox), _mm_mul_ps(mS[i], p05)),
                                  _mm_add_ps(_mm_mul_ps(yS[i], x), cS[i]));
#if DEBUG_WITH_PRINT
            if (rangeMask[i][0] != 0)
                std::cout << _D(x[0]) << _D(rangeMask[i][0]) << _D(xS[i][0]) << _D(xS[i + 1][0])
                          << _D(ox[0]) << _D(cS[i][0]) << _D(mS[i][0]) << _D(yS[i][0])
                          << _D(val[i][0]) << _D(adVal[i][0]) << std::endl;
#endif
        }
        auto res = _mm_and_ps(rangeMask[0], val[0]);
        auto adres = _mm_and_ps(rangeMask[0], adVal[0]);
        for (int i = 1; i < pts - 1; ++i)
        {
            res = _mm_add_ps(res, _mm_and_ps(rangeMask[i], val[i]));
            adres = _mm_add_ps(adres, _mm_and_ps(rangeMask[i], adVal[i]));
        }
        f = res;
        adf = adres;
    }
    float xs[pts], ys[pts], dxs[pts], slopes[pts], intercepts[pts];

    __m128 xS[pts], yS[pts], dxS[pts], mS[pts], cS[pts];
};

void singleFoldADAA(__m128 x, __m128 &f, __m128 &adf)
{
    static auto folder = FolderADAA<4>({-10, -0.7, 0.7, 10}, {-1, 1, -1, 1});
    folder.evaluate(x, f, adf);
}

void westCoastFoldADAA(__m128 x, __m128 &f, __m128 &adf)
{
    // Factors based on
    // DAFx-17 DAFX-194 Virtual Analog Buchla 259 Wavefolder
    // by Sequeda, Pontynen, Valimaki and Parker
    static auto folder = FolderADAA<14>(
        {-10, -2, -1.0919091909190919, -0.815881588158816, -0.5986598659865987, -0.3598359835983597,
         -0.11981198119811971, 0.11981198119811971, 0.3598359835983597, 0.5986598659865987,
         0.8158815881588157, 1.0919091909190919, 2, 10},
        {1, 0.9, -0.679765619488133, 0.5309659972270625, -0.6255506631744251, 0.5991799179917987,
         -0.5990599059905986, 0.5990599059905986, -0.5991799179917987, 0.6255506631744251,
         -0.5309659972270642, 0.679765619488133, -0.9, -1});
    folder.evaluate(x, f, adf);
}

const int n_filter_registers = 16;
const int n_waveshaper_registers = 4;

struct alignas(16) QuadFilterWaveshaperState
{
    __m128 R[n_waveshaper_registers];
    __m128 init;
};

template <void FandADF(__m128, __m128 &, __m128 &), int xR, int aR, bool updateInit = true>
__m128 ADAA(QuadFilterWaveshaperState *__restrict s, __m128 x)
{
    auto xPrior = s->R[xR];
    auto adPrior = s->R[aR];

    __m128 f, ad;
    FandADF(x, f, ad);

    auto dx = _mm_sub_ps(x, xPrior);
    auto dad = _mm_sub_ps(ad, adPrior);

    const static auto tolF = 0.0001;
    const static auto tol = _mm_set1_ps(tolF), ntol = _mm_set1_ps(-tolF);
    auto ltt = _mm_and_ps(_mm_cmplt_ps(dx, tol), _mm_cmpgt_ps(dx, ntol)); // dx < tol && dx > -tol
    ltt = _mm_or_ps(ltt, s->init);
    auto dxDiv = _mm_rcp_ps(_mm_add_ps(_mm_and_ps(ltt, tol), _mm_andnot_ps(ltt, dx)));

    auto fFromAD = _mm_mul_ps(dad, dxDiv);
    auto r = _mm_add_ps(_mm_and_ps(ltt, f), _mm_andnot_ps(ltt, fFromAD));

    s->R[xR] = x;
    s->R[aR] = ad;
    if (updateInit)
    {
        s->init = _mm_setzero_ps();
    }

    return r;
}

// End Surge waveshaper code

/*
Adapted from code by "mdsp" in https://www.kvraudio.com/forum/viewtopic.php?t=70372
*/

template<size_t CoeffSz>
inline float chebyshev(float x, const std::array<float,CoeffSz>& A, int order)
{
	float Tn_2 = 1.0f; 
	float Tn_1 = x;
	float Tn;
	float out = A[0]*Tn_1;

	for(int n=2;n<=order;n++)
	{
		Tn	 =	2.0f*x*Tn_1 - Tn_2;
		out	 +=	A[n-1]*Tn;		
		Tn_2 =	Tn_1;
		Tn_1 =  Tn;
	}
	return out;
}

template<size_t CoeffSz>
inline simd::float_4 chebyshev(simd::float_4 x, const std::array<float,CoeffSz>& A, int order)
{
	simd::float_4 Tn_2 = 1.0f; 
	simd::float_4 Tn_1 = x;
	simd::float_4 Tn;
    simd::float_4 out = A[0]*Tn_1;

	for(int n=2;n<=order;n++)
	{
		Tn	 =	2.0f*x*Tn_1 - Tn_2;
		out	 +=	A[n-1]*Tn;		
		Tn_2 =	Tn_1;
		Tn_1 =  Tn;
	}
	return out;
}

inline simd::float_4 fmodex(simd::float_4 x, float y=1.0f)
{
    x = simd::fmod(x,y);
    simd::float_4 a = y - (-y * x);
    simd::float_4 neg = x >= 0.0f;
    return simd::ifelse(neg,x,a);
}

// fold value to range -1,1
inline simd::float_4 reflectx(simd::float_4 x)
{
    x = 0.5f*(x-1.0f);
    simd::float_4 temp = simd::fmod(x,2.0f);
    simd::float_4 temp2 = temp + 2.0f;
    simd::float_4 temp3 = simd::ifelse(x >= 0.0f,temp,temp2);  
    return -1.0f + 2.0f * simd::abs(temp3-1.0f);
}

template<typename GridT>
inline void quantize_to_scale(float x, const std::vector<GridT>& g,
    float& out1, float& out2, float& outdiff)
{
    if (g.empty()) // special handling for no scale
    {
        //out1 = x;
        //out2 = x;
        //outdiff = 0.5f;
        //return;
        out1 = x;
        float maxd = 10.0f;
        float dtune = rescale(x,rack::dsp::FREQ_C4/16.0f,20000.0f,0.0f,maxd);
        dtune = clamp(dtune,0.0f,maxd);
        float m = std::fmod(x,maxd);
        out2 = x+dtune;
        outdiff = m*(1.0f/maxd);
        return;
    }
    auto t1=std::upper_bound(std::begin(g),std::end(g),x);
    if (t1<std::end(g)-1)
    {
        auto t0=std::begin(g);
        if (t1>std::begin(g))
            t0=t1-1;
        out1 = *t0;
        out2 = *t1;
        if (out1 == out2)
        {
            outdiff = 0.0f;
            return;
        }
        outdiff = rescale(x,out1,out2,0.0,1.0);
        return;
    }
    out1 = g.back();
    out2 = g.back();
    outdiff = 1.0f;
}

inline float averterMap(float x, float shape)
{
    if (shape == 0.0f)
        return x;
    if (x<0.0f)
    {
        x += 1.0f;
        float temp = 1.0f-std::pow(1.0f-x,shape);
        return temp - 1.0f;
    }
    return std::pow(x,shape);
}

class alignas(16) SIMDSimpleOsc
{
public:
    std::array<float,512> warp3table;
    SIMDSimpleOsc()
    {
        for (int i=0;i<warp3table.size();++i)
        {
            float x = rescale(float(i),0,warp3table.size()-1,0.0f,1.0f);
            const float n1 = 7.5625;
            const float d1 = 2.75;
            float y = x;
            if (x < 1 / d1) {
                y = n1 * x * x;
            } else if (x < 2 / d1) {
                y = n1 * (x -= 1.5 / d1) * x + 0.75;
            } else if (x < 2.5 / d1) {
                y = n1 * (x -= 2.25 / d1) * x + 0.9375;
            } else {
                y = n1 * (x -= 2.625 / d1) * x + 0.984375;
            }
            warp3table[i] = y;
        }
    }
    simd::float_4 m_phase = 0.0;
    simd::float_4 m_phase_inc = 0.0;
    
    float m_warp = 0.0f;
    int m_warp_mode = 2;
    void prepare(int numchans, double samplerate)
    {
        
    }
    void setFrequencies(float hz0, float hz1, float samplerate)
    {
        simd::float_4 hzs(hz0,hz1,hz0,hz1);
        m_phase_inc = simd::float_4(1.0f/samplerate)*hzs;
    }
    void setFrequencies(simd::float_4 hzs, float samplerate)
    {
        m_phase_inc = simd::float_4(1.0f/samplerate)*hzs;
    }
    float m_warp_steps = 128.0f;
    void setPhaseWarp(int mode, float amt)
    {
        amt = clamp(amt,0.0f,1.0f);
        m_warp_mode = mode;
        m_warp_steps = std::pow(2.0f,2.0f+(1.0f-amt)*4.0f);
        if (m_warp_mode<2)
            m_warp = std::pow(amt,2.0f);
        else m_warp = 1.0f-std::pow(1.0f-amt,2.0f);
    }
    simd::float_4 processSample(float)
    {
        simd::float_4 phase_to_use = m_phase;
        simd::float_4 gain = 1.0f;
//#ifdef FOOSIMD
        if (m_warp_mode == 0)
        {
            phase_to_use = simd::float_4(1.0)+simd::float_4(m_warp)*simd::float_4(7.0f);
            phase_to_use = m_phase * phase_to_use;
            phase_to_use = simd::fmin(phase_to_use,1.0f);
            //if (phase_to_use>1.0)
            //    phase_to_use = 1.0f;
        } else if (m_warp_mode == 1)
        {
            double steps = m_warp_steps; // std::pow(2.0f,2.0f+(1.0-m_warp)*6.0f);
            
            phase_to_use = simd::round(m_phase*steps)/steps;
        }
        else
        {
            
            for (int i=0;i<4;++i)
            {
                float ph = m_phase[i];
                int ind = ph * (warp3table.size()-1);
                ind = clamp(ind,0,warp3table.size()-1);
                ph = warp3table[ind];
                phase_to_use[i] = (1.0f-m_warp) * phase_to_use[i] + m_warp * ph;
            }
            /*
            float multip = 1.0+std::round(m_warp*8.0f);
            for (int i=0;i<4;++i)
            {
                float ph = wrap_value(0.0f,m_phase[i]*multip,1.0f);
                
                //int ind = ph * (warp3table.size()-1);
                //ind = clamp(ind,0,warp3table.size()-1);
                //ph = warp3table[ind];
                phase_to_use[i] = ph; // (1.0f-m_warp) * phase_to_use[i] + m_warp * ph;
            }
            */
        }
//#endif
        simd::float_4 rs = simd::sin(simd::float_4(2*g_pi)*phase_to_use);
        //float r = std::sin(2*3.14159265359*phase_to_use);
        m_phase += m_phase_inc;
        //m_phase = std::fmod(m_phase,1.0);
        //m_phase = wrap_value(0.0,m_phase,1.0);
        //m_phase = simd::fmod(m_phase,simd::float_4(1.0f));
        m_phase = fmodex(m_phase);
        return rs*gain;
    }
};


class SimpleOsc
{
public:
    SimpleOsc()
    {

    }
    double m_phase = 0.0;
    double m_phase_inc = 0.0;
    double m_samplerate = 44100;
    float m_warp = 0.0f;
    int m_warp_mode = 2;
    void prepare(int numchans, double samplerate)
    {
        m_samplerate = samplerate;
    }
    void setFrequency(float hz)
    {
        //if (hz<0.01f)
        //    hz = 0.01f;
        m_phase_inc = 1.0/m_samplerate*hz;
    }
    float m_warp_steps = 128.0f;
    void setPhaseWarp(int mode, float amt)
    {
        amt = clamp(amt,0.0f,1.0f);
        m_warp_mode = mode;
        m_warp_steps = std::pow(2.0f,2.0f+(1.0f-amt)*6.0f);
        m_warp = std::pow(amt,2.0f);
    }
    float processSample(float)
    {
        double phase_to_use;
        if (m_warp_mode == 0)
        {
            /*
            if (m_warp!=0.5f)
            {
                double w;
                if (m_warp<0.5)
                    w = rescale(m_warp,0.0f,0.5,0.2f,1.0f);
                else
                    w = rescale(m_warp,0.5f,1.0f,1.0f,4.0f);
                phase_to_use = std::pow(m_phase,w);
            } else
                phase_to_use = m_phase;
            */
            phase_to_use = 1.0+m_warp*7.0;
            phase_to_use = m_phase * phase_to_use;
            if (phase_to_use>1.0)
                phase_to_use = 1.0f;
        } else if (m_warp_mode == 1)
        {
            double steps = m_warp_steps; // std::pow(2.0f,2.0f+(1.0-m_warp)*6.0f);
            /*
            double skewp = 0.05;
            if (m_warp<skewp)
            {
                steps = rescale(m_warp,0.00f,skewp,128.0f,16.0f);
            } else
            {
                steps = rescale(m_warp,skewp,1.0f,16.0f,3.0f);
                
            }
            */
            phase_to_use = std::round(m_phase*steps)/steps;
        }
        else
        {
            double pmult = rescale(m_warp,0.0f,1.0f,0.5f,2.0f);
            phase_to_use = std::fmod(pmult*m_phase,1.0);
        }
        float r = std::sin(2*g_pi*phase_to_use);
        m_phase += m_phase_inc;
        //m_phase = std::fmod(m_phase,1.0);
        m_phase = wrap_value(0.0,m_phase,1.0);
        //if (m_phase>=1.0f)
        //    m_phase-=m_phase_inc;
        return r;
    }
};



class KlangScale
{
public:
    KlangScale() {}
    // Construct from Scale file 
    KlangScale(std::string fn)
    {
        double root_freq = dsp::FREQ_C4/16.0;
        try
            {
                auto thescale = Tunings::readSCLFile(fn);
                pitches = semitonesFromScalaScale<double>(thescale,0.0,128.0);
                name = thescale.name;
                
            }
            catch (std::exception& excep)
            {
                name = "Continuum";
            }
    }
    std::vector<double> pitches;
    std::string name;
};

class KlangScaleBank
{
public:
    std::vector<KlangScale> scales;
    std::string description;
};

class ScaleOscillator
{
public:
    float m_gain_smooth_amt = 0.999f;
    KlangScale fallBackScale;
    alignas(16) std::array<float,32> mChebyCoeffs;
    KlangScale& getScaleChecked(int banknum, int scalenum)
    {
        if (banknum>=0 && banknum<m_all_banks.size())
        {
            auto& curbank = m_all_banks[banknum];
            if (scalenum>=0 && scalenum<curbank.scales.size())
            {
                return curbank.scales[scalenum];
            }
        }
        return fallBackScale;
    }
    KlangScale& getCurrentScale()
    {
        return getScaleChecked(m_cur_bank,m_curScale);
    }
    std::string getScaleName()
    {
        auto& s = getCurrentScale();
        if (s.name.empty()==false)
            return s.name;
        return "Invalid scale index";
    }
    
    
    ScaleOscillator()
    {
        for (int i=0;i<mChebyCoeffs.size();++i)
            mChebyCoeffs[i] = 0.0f;
        m_fold_smoother.setAmount(0.99);
        for (int i=0;i<m_oscils.size();++i)
        {
            m_oscils[i].prepare(1,44100.0f);
            m_osc_gain_smoothers[i*2+0].setAmount(m_gain_smooth_amt);
            m_osc_gain_smoothers[i*2+1].setAmount(m_gain_smooth_amt);
            m_osc_freq_smoothers[i*2+0].setAmount(0.99);
            m_osc_freq_smoothers[i*2+1].setAmount(0.99);
            m_osc_gains[i*2+0] = 1.0f;
            m_osc_gains[i*2+1] = 1.0f;
            m_osc_freqs[i*2+0] = 440.0f;
            m_osc_freqs[i*2+1] = 440.0f;
            fms[i] = 0.0f;
            m_unquant_freqs[i] = 1.0f;
        }
        m_norm_smoother.setAmount(0.999);
        
        KlangScaleBank bank_a;
        bank_a.description = "Just intoned stacked intervals";
        KlangScale continuumScale;
        continuumScale.name = "Continuum";
        bank_a.scales.push_back(continuumScale);
        
        std::vector<std::string> scalafiles;
        #ifndef RAPIHEADLESS
        std::string dir = asset::plugin(pluginInstance, "res/scala_scales");
        #else
        std::string dir;
        #endif
        scalafiles.push_back(dir+"/syntonic_comma.scl");
        scalafiles.push_back(dir+"/major_tone_ji.scl");
        scalafiles.push_back(dir+"/minor_third_ji.scl");
        scalafiles.push_back(dir+"/major_third_ji.scl");
        scalafiles.push_back(dir+"/fourth_ji.scl");
        scalafiles.push_back(dir+"/pure fifths.scl");
        scalafiles.push_back(dir+"/minor_sixth_ji.scl");
        scalafiles.push_back(dir+"/major_sixth_ji.scl");
        scalafiles.push_back(dir+"/minor_seventh_ji.scl");
        scalafiles.push_back(dir+"/major_seventh_ji.scl");
        scalafiles.push_back(dir+"/octave.scl");
        scalafiles.push_back(dir+"/ninth1_ji.scl");
        scalafiles.push_back(dir+"/ninth2_ji.scl");

        for(auto& fn : scalafiles)
        {
            bank_a.scales.emplace_back(fn);
        }
        m_all_banks.push_back(bank_a);
        scalafiles.clear();
        KlangScaleBank bank_b;
        bank_b.description = "Sundry scales";

        scalafiles.push_back(dir+"/penta_opt.scl");
        scalafiles.push_back(dir+"/Ancient Greek Archytas Enharmonic.scl");
        scalafiles.push_back(dir+"/Ancient Greek Archytas Diatonic.scl");
        scalafiles.push_back(dir+"/partch_43.scl");
        scalafiles.push_back(dir+"/octone.scl");
        scalafiles.push_back(dir+"/Chopi Xylophone.scl");
        scalafiles.push_back(dir+"/Indonesian Pelog.scl");
        scalafiles.push_back(dir+"/05-19.scl");
        scalafiles.push_back(dir+"/13-19.scl");
        scalafiles.push_back(dir+"/bohlen_quintuple_j.scl");
        scalafiles.push_back(dir+"/bohlen_11.scl");
        scalafiles.push_back(dir+"/ED3o2-04.scl");
        scalafiles.push_back(dir+"/equally tempered minor.scl");
        scalafiles.push_back(dir+"/12tet.scl");
        scalafiles.push_back(dir+"/tritones.scl");
        scalafiles.push_back(dir+"/tetra01.scl");
        scalafiles.push_back(dir+"/xenakis_jonchaies.scl");
        scalafiles.push_back(dir+"/xenakis_mists.scl");
        scalafiles.push_back(dir+"/19tet_sieve01.scl");
        scalafiles.push_back(dir+"/19tet_sieve02.scl");
        scalafiles.push_back(dir+"/weird01.scl");
        for(auto& fn : scalafiles)
        {
            bank_b.scales.emplace_back(fn);
        }
        double root_freq = dsp::FREQ_C4/8.0;
        double freq = root_freq;
        KlangScale scale;
        scale.name = "Harmonics";
        int i=1;
        while (freq<20000.0)
        {
            freq = i * root_freq;
            double p = 12.0 * log2(freq/root_freq);
            if (p>128.0f)
                break;
            scale.pitches.push_back(p);
            ++i;
        }
        bank_b.scales.push_back(scale);
        
        m_all_banks.push_back(bank_b);
        scalafiles.clear();
        
        KlangScaleBank bank_c;
        bank_c.description = "Chords";

        scalafiles.push_back(dir+"/major_chord_et.scl");
        scalafiles.push_back(dir+"/major_chord_ji.scl");
        scalafiles.push_back(dir+"/minor_chord_et.scl");
        scalafiles.push_back(dir+"/minor_chord_ji.scl");
        scalafiles.push_back(dir+"/dominant 7th 1.scl");
        scalafiles.push_back(dir+"/ninth_chord_et.scl");
        scalafiles.push_back(dir+"/ninth_chord2_et.scl");
        
        for(auto& fn : scalafiles)
        {
            bank_c.scales.emplace_back(fn);
        }
        m_all_banks.push_back(bank_c);
        KlangScaleBank bank_d;
        bank_d.description = "User bank";
        for (int i=0;i<8;++i)
        {
            bank_d.scales.push_back(KlangScale());
            bank_d.scales.back().name = "Slot "+std::to_string(i+1);
        }
        m_all_banks.push_back(bank_d);
        
        m_scale.reserve(2048);
        m_scale = getScaleChecked(0,1).pitches;
        for (int i=0;i<mExpFMPowerTable.size();++i)
        {
            float x = rescale(i,0,mExpFMPowerTable.size()-1,-60.0f,60.0f);
            x = std::pow(2.0f,x/12.0f);
            mExpFMPowerTable[i] = x;
        }
        mChebyMorphSmoother.setAmount(0.999);
        for (int i=0;i<chebyMorphCount+1;++i)
        {
            double sum = 0.0;
            for (int j=0;j<16;++j)
                sum+=chebyMorphCoeffs[i][j];
            double gain = 1.0/sum;
            for (int j=0;j<16;++j)
                chebyMorphCoeffs[i][j] *= gain;
        }
        refreshChebyCoeffs();
        updateOscFrequencies();
    }
    void refreshChebyCoeffs()
    {
        #ifndef RAPIHEADLESS
        std::string chebyfn = asset::userDir+"/klang_cheby_table.txt";
        loadChebyshevCoefficients(chebyfn);
        #endif
    }
    inline float getExpFMDepth(float semitones)
    {
        float x = rescale(semitones,-60.0f,60.0f,0.0f,(float)mExpFMPowerTable.size()-1);
        int index = x;
        index = clamp(index,0,mExpFMPowerTable.size()-1);
        return mExpFMPowerTable[index];
    }
    int getNumBanks() { return m_all_banks.size(); }
    void loadChebyshevCoefficients(std::string fn)
    {
        // fill default morph table in case opening the file fails
        for (int i=0;i<chebyMorphCount+1;++i)
            {
                for (int j=0;j<16;++j)
                {
                    if (j == 0)
                        chebyMorphCoeffs[i][j] = 1.0f;
                    else chebyMorphCoeffs[i][j] = 0.0f;
                }
                    
                
            }
        std::fstream instream{fn};
        if (instream.is_open())
	    {
            
            std::vector<std::string> lines;
            char buf[4096];
            while (instream.eof() == false)
            {
                instream.getline(buf, 4096);
                lines.push_back(buf);
            }
            //DEBUG("KLANG num cheby lines %d",lines.size());
            for (int j=0;j<lines.size();++j)
            {
                if (j==chebyMorphCount)
                    break;
                auto tokens = string::split(lines[j],",");
                //DEBUG("KLANG num tokens %d %d",j,tokens.size());
                for(int i=0;i<tokens.size();++i)
                {
                    float coeff = std::atof(tokens[i].c_str());
                    if (i<16)
                    {
                        chebyMorphCoeffs[j][i] = clamp(coeff,-1.0f,1.0f);
                    }
                    if (j == 0)
                    {
                        //DEBUG("KLANG cheby %f",chebyMorphCoeffs[j][i]);
                    }
                }
            }
            // copy second to last array to last array for interpolation
            for (int i=0;i<16;++i)
            {
                chebyMorphCoeffs[chebyMorphCount][i] = chebyMorphCoeffs[chebyMorphCount-1][i];
            }
            // normalize
            for (int i=0;i<chebyMorphCount+1;++i)
            {
                float sum = 0.0f;
                for (int j=0;j<16;++j)
                {
                    sum += std::abs(chebyMorphCoeffs[i][j]);
                }
                if (sum>0.0f)
                    sum = 1.0f / sum;
                else sum = 1.0f;
                for (int j=0;j<16;++j)
                {
                    chebyMorphCoeffs[i][j] *= sum;
                }
            }
        }
    }
    int m_pitchQuantizeMode = 0;
    void setPitchQuantizeMode(int m)
    {
        m_pitchQuantizeMode = clamp(m,0,1);
    }
    void updateOscFrequencies()
    {
        auto xfades = makeArray<float,16>();
        int lastoscili = m_active_oscils-1;
        if (lastoscili==0)
            lastoscili = 1;
        if (mDoScaleChange==true)
        {
            mDoScaleChange = false;
            m_scale = mScaleToChangeTo;
        }
        double rootf = rack::dsp::FREQ_C4/16.0;
        for (int i=0;i<m_active_oscils;++i)
        {
            float normpos = rescale((float)i,0,lastoscili,0.0f,1.0f);
            
            if (m_spread_dist<0.5)
            {
                float sh = rescale(m_spread_dist,0.0f,0.5f,3.0f,1.0f);
                normpos = std::pow(normpos,sh);
            } else
            {
                float sh = rescale(m_spread_dist,0.5f,1.0f,1.0f,3.0f);
                normpos = 1.0f-std::pow(1.0f-normpos,sh);
            }
            float pitch = 0.0f;
            float p0 = 0.0f;
            float p1 = 0.0f;
            float diff = 0.5f;
            if (m_pitchQuantizeMode == 0)
            {
                pitch = 36.0f + rescale(normpos,0.0f,1.0f, m_root_pitch,m_root_pitch+(96.0f*m_spread));
                quantize_to_scale(pitch,m_scale,p0,p1,diff);
            }
            else if (m_pitchQuantizeMode == 1) // get pitch directly from scale
            {
                if (m_scale.empty() == false)
                {
                    float steproot = rescale(m_root_pitch,-36.0f,36.0f,0.0f,(m_scale.size()-1));
                    float scalestepf = steproot + rescale(normpos,0.0f , 1.0f , 0.0f, (m_scale.size()-1)*m_spread);
                    scalestepf = clamp(scalestepf,0.0f,m_scale.size()-1);
                    int scalestepi0 = scalestepf;
                    int scalestepi1 = scalestepi0 + 1;
                    if (scalestepi1>m_scale.size()-1)
                        scalestepi1 = m_scale.size()-1;
                    diff = scalestepf-(int)scalestepf;
                    p0 = m_scale[scalestepi0];
                    p1 = m_scale[scalestepi1];
                } else
                {
                    pitch = 36.0f + rescale(normpos,0.0f,1.0f, m_root_pitch,m_root_pitch+(72.0f*m_spread));
                    quantize_to_scale(pitch,m_scale,p0,p1,diff);
                }
                
            }
            m_unquant_freqs[i] = pitch;
            float f0 = rootf*std::pow(dsp::FREQ_SEMITONE,p0);
            float f1 = rootf*std::pow(dsp::FREQ_SEMITONE,p1);
            if (mXFadeMode == 0)
                xfades[i] = 0.0f;
            else if (mXFadeMode == 1)
            {
                if (diff<0.25f)
                    diff = 0.0f;
                else if (diff>=0.25f && diff<0.75f)
                    diff = rescale(diff,0.25f,0.75f,0.0f,1.0f);
                else diff = 1.0f;
                xfades[i] = diff;
            }
            else if (mXFadeMode == 2)
                xfades[i] = diff;
            else 
            {
                float temp = clamp(diff,0.0f,1.0f);
                xfades[i] = std::sqrt(temp);
            }
            float detun0 = rescale((float)i,0,m_active_oscils,0.0f,f0*0.10f*m_detune);
            float detun1 = rescale((float)i,0,m_active_oscils,0.0f,f1*0.10f*m_detune);
            if (i % 2 == 1)
            {
                detun0 = -detun0;
                detun1 = -detun1;
            }
                
            f0 = clamp(f0*m_freqratio+detun0,1.0f,20000.0f);
            f1 = clamp(f1*m_freqratio+detun1,1.0f,20000.0f);
            if (mFreeze_enabled == false)
            {
                m_osc_freqs[i*2+0] = f0;
                m_osc_freqs[i*2+1] = f1;
            } else
            {
                if (mFreeze_mode == 0)
                {
                    if (i % 2 == 1)
                    {
                        m_osc_freqs[i*2+0] = f0;
                        m_osc_freqs[i*2+1] = f1;
                    }
                }
                else if (mFreeze_mode == 1)
                {
                    if (i>=m_active_oscils/2)
                    {
                        m_osc_freqs[i*2+0] = f0;
                        m_osc_freqs[i*2+1] = f1;
                    }
                } else
                {
                    if (i > 0)
                    {
                        m_osc_freqs[i*2+0] = f0;
                        m_osc_freqs[i*2+1] = f1;
                    }
                }
            }
            
            
        }
        float xs0 = 0.0f;
        float xs1 = 1.0f;
        float ys0 = 1.0f;
        float ys1 = 1.0f;
        if (m_balance<0.25f)
        {
            xs0 = 0.0f;
            ys0 = 1.0f;
            xs1 = rescale(m_balance,0.0f,0.25f,0.01f,1.0f);
            ys1 = 0.0f;
        } else if (m_balance>=0.25f && m_balance<0.5f)
        {
            xs0 = 0.0f;
            ys0 = 1.0f;
            xs1 = 1.0f;
            ys1 = rescale(m_balance,0.25f,0.5f,0.0f,1.0f);
        } else if (m_balance>=0.5f && m_balance<0.75f)
        {
            xs0 = 0.0f;
            ys0 = rescale(m_balance,0.5f,0.75f,1.0f,0.0f);
            xs1 = 1.0f;
            ys1 = 1.0f;
        } else 
        {
            xs0 = rescale(m_balance,0.75f,1.0f,0.0f,0.99f);
            ys0 = 0.0f;
            xs1 = 1.0f;
            ys1 = 1.0f;
        }
        for (int i=0;i<m_oscils.size();++i)
        {
            float bypassgain = 0.0f;
            if (i<m_active_oscils)
                bypassgain = 1.0f;
            float normx = rescale((float)i,0,lastoscili,0.0f,1.0f);
            float amp = 0.0f;
            if (normx<=1.0f)
            {
                amp = rescale(normx,xs0,xs1,ys0,ys1);
                amp = clamp(amp,0.0f,1.0f);
            }
            float gain = amp*bypassgain;
            m_osc_gains[i*2+0] = gain*(1.0-xfades[i]);
            m_osc_gains[i*2+1] = gain*xfades[i];
        }
    }
    std::array<float,16> fms;
    void processNextFrame(float* outbuf, float samplerate)
    {
        int oscilsused = 0;
        int lastosci = m_active_oscils-1;
        if (m_fm_algo<2)
        {
            float hz0 = m_osc_freq_smoothers[0].process(m_osc_freqs[0]);
            float hz1 = m_osc_freq_smoothers[1].process(m_osc_freqs[1]);
            m_oscils[0].setFrequencies(hz0,hz1,samplerate);
        }
            
        else
        {
            float hz0 = m_osc_freq_smoothers[lastosci*2+0].process(m_osc_freqs[lastosci*2+0]);
            float hz1 = m_osc_freq_smoothers[lastosci*2+1].process(m_osc_freqs[lastosci*2+1]);
            m_oscils[lastosci].setFrequencies(hz0,hz1,samplerate);
        }
        float smorph = mChebyMorphSmoother.process(mChebyMorph);
        if (m_fold_algo == 1) // && smorph>0.00001f)
        {
            const int h = chebyMorphCount-1;
            int i0 = smorph * h;
            int i1 = i0 + 1;
            float temp = smorph * h;
            float xfrac = temp - (int)temp;
            /*
            for (int i=0;i<16;++i)
            {
                float y0 = chebyMorphCoeffs[i0][i];
                float y1 = chebyMorphCoeffs[i1][i];
                float interpolated = y0+(y1-y0)*xfrac;
                mChebyCoeffs[i] = interpolated;
            }
            */
            
            for (int i=0;i<16;i+=4)
            {
                simd::float_4 y0 = simd::float_4::load(&chebyMorphCoeffs[i0][i]);
                simd::float_4 y1 = simd::float_4::load(&chebyMorphCoeffs[i1][i]);
                simd::float_4 interpolated = y0 + (y1 - y0) * xfrac;
                interpolated.store(&mChebyCoeffs[i]);
            }
            
           /*
           float xs0 = 0.0f;
           float ys0 = rescale(smorph,0.0f,1.0f,1.0f,0.0f);
           float xs1 = rescale(smorph,0.0f,1.0f,0.01f,1.0f);
           float ys1 = 0.0f; //rescale(smorph,0.0f,1.0f,0.00f,0.25f);
           //float gainshape = rescale(smorph,0.0f,1.0f,1.0f,0.125f);
           float csum = 0.0f;
           for (int i=0;i<16;++i)
           {
               float z0 = rescale((float)i,0,15,0.0f,1.0f);
               float interpolated = rescale(z0,xs0,xs1,ys0,ys1);
               mChebyCoeffs[i] = clamp(interpolated,0.0f,1.0f);
               csum += mChebyCoeffs[i];
           }
           csum = 1.0f/csum;
           for (int i=0;i<16;++i)
           {
               mChebyCoeffs[i] *= csum;
           }
           */
            auto bfunc=[](float x, float middle, float width)
            {
                const float maxlev = 0.5f;
                if (x<=0.0f)
                    return maxlev;
                float result = 0.0f;
                float left = middle-width;
                float right = middle+width;
                if (x>=left && x<middle)
                {
                    result = rescale(x,left,middle,0.0f,maxlev);
                } else if (x>=middle && x<right)
                {
                    result = rescale(x,middle,right,maxlev,0.0f);
                }
                if (left<0.0f)
                {
                    //float gain = rescale(middle,0.0f,right,0.0f,1.0f);
                    //result *= gain;
                }
                    
                return result;
                return 0.0f;
            };
            float bs = 0.0f;
            float zzz = rescale(smorph,0.0f,1.0f,-0.25f,1.0f);
            for (int i=0;i<16;++i)
            {
                float x = rescale((float)i,0,15,0.0f,1.0f);
                x = bfunc(x,zzz,0.25f);
                //mChebyCoeffs[i] = clamp(x,0.0f,1.0f);
                //bs += mChebyCoeffs[i];
            }
            //bs = 1.0f/bs;
            //for (int i=0;i<16;++i)
            //    mChebyCoeffs[i] *= bs;
            
        }
        
        //float foldgain = m_fold_smoother.process((1.0f+m_fold*8.0f));
        float foldgain = m_fold_smoother.process(m_fold);
        for (int i=0;i<m_oscils.size();++i)
        {
            float gain0 = m_osc_gain_smoothers[i*2+0].process(m_osc_gains[i*2+0]);
            float gain1 = m_osc_gain_smoothers[i*2+1].process(m_osc_gains[i*2+1]);
            simd::float_4 ss = m_oscils[i].processSample(0.0f);
            float s0 = ss[0];
            float s1 = ss[1];
            fms[i] = s0;
            float s2 = s0 * gain0 + s1 * gain1;
            outbuf[i] = s2;
        }
        if (m_fold_algo == 0)
        {
            float foldg = (1.0f+foldgain*8.0f);
            for (int i=0;i<m_oscils.size();i+=4)
            {
                simd::float_4 x = simd::float_4::load(&outbuf[i]);
                x = reflectx(x*foldg);
                x.store(&outbuf[i]);
            }
        } 
        else if (m_fold_algo == 1) // && smorph>0.00001f)
        {
            for (int i=0;i<m_oscils.size();i+=4)
            {
                simd::float_4 x = simd::float_4::load(&outbuf[i]);
                x = chebyshev(x,mChebyCoeffs,16);
                x = clamp(x,simd::float_4(-1.0f),simd::float_4(1.0f));
                x.store(&outbuf[i]);
            }
        } else if (m_fold_algo == 2)
        {
            float foldg = (0.15f+foldgain*4.0f);
            for (int i=0;i<m_oscils.size();i+=4)
            {
                simd::float_4 x = simd::float_4::load(&outbuf[i]);
                x *= foldg;
                x = ADAA<westCoastFoldADAA,0,1>(&mShaperStates[i],x.v);
                x.store(&outbuf[i]);
            }
        }
        int fm_mode = m_fm_algo;
        auto fmfunc = [this](int mode, float& basefreq, int mi)
        {
            if (mode == 0)
                basefreq += fms[mi]*m_fm_amt*basefreq*2.0f;
            else if (mode == 1)
                basefreq *= getExpFMDepth(fms[mi]*m_fm_amt*60.0f);
                //basefreq *= std::pow(dsp::FREQ_SEMITONE,fms[mi]*m_fm_amt*60.0f);
            else if (mode == 2)
                basefreq += dsp::FREQ_C4*fms[mi]*m_fm_amt*5.0f;  
        };
        
        if (fm_mode == 0)
        {
            for (int i=1;i<m_active_oscils;++i)
            {
                float hz0 = m_osc_freq_smoothers[i*2+0].process(m_osc_freqs[i*2+0]);
                fmfunc(m_fm_mod_mode,hz0,0);
                float hz1 = m_osc_freq_smoothers[i*2+1].process(m_osc_freqs[i*2+1]);
                fmfunc(m_fm_mod_mode,hz1,0);
                m_oscils[i].setFrequencies(hz0,hz1,samplerate);
            }
        } else if (fm_mode == 1)
        {
            for (int i=1;i<m_active_oscils;++i)
            {
                float hz0 = m_osc_freq_smoothers[i*2+0].process(m_osc_freqs[i*2+0]);
                fmfunc(m_fm_mod_mode,hz0,i-1);
                float hz1 = m_osc_freq_smoothers[i*2+1].process(m_osc_freqs[i*2+1]);
                fmfunc(m_fm_mod_mode,hz1,i-1);
                m_oscils[i].setFrequencies(hz0,hz1,samplerate);
            }
        } else if (fm_mode == 2)
        {
            for (int i=0;i<m_active_oscils-1;++i)
            {
                float hz0 = m_osc_freq_smoothers[i*2+0].process(m_osc_freqs[i*2+0]);
                fmfunc(m_fm_mod_mode,hz0,lastosci);
                float hz1 = m_osc_freq_smoothers[i*2+1].process(m_osc_freqs[i*2+1]);
                fmfunc(m_fm_mod_mode,hz1,lastosci);
                m_oscils[i].setFrequencies(hz0,hz1,samplerate);
            }
        }
    }
    int m_curScale = 0;
    void setScale(float x)
    {
        
        x = clamp(x,0.0f,1.0f);
        auto& bank = m_all_banks[m_cur_bank];
        int i = x * (bank.scales.size()-1);
        if (i!=m_curScale)
        {
            m_curScale = i;
            m_scale = getScaleChecked(m_cur_bank,m_curScale).pitches;
        }
    }
    void loadScaleFromFile(std::string fn)
    {
        if (m_cur_bank == m_all_banks.size()-1)
        {
            KlangScale scale(fn);
            auto& s = getScaleChecked(m_cur_bank,m_curScale);
            if (s.name.empty()==false)
            {
                s = scale;
                mScaleToChangeTo = scale.pitches;
                mDoScaleChange = true;
                // sleep here so that we won't get back here while the switch is done in the audio thread
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        
        
    }
    json_t* dataToJson() 
    {
        json_t* resultJ = json_object();
        json_t* slotsJ = json_array();
        auto& bank = m_all_banks.back();
        for (int i=0;i<bank.scales.size();++i)
        {
            auto& scale = bank.scales[i];
            auto stringj = json_string(scale.name.c_str());
            json_array_append(slotsJ,stringj);
        }
        json_object_set(resultJ,"userscalafiles",slotsJ);
        return resultJ;
    }
    void dataFromJson(json_t* root)
    {
        if (!root)
            return;
        auto& bank = m_all_banks.back();
        auto slotsJ = json_object_get(root,"userscalafiles");
        int lastbindex = m_all_banks.size()-1;
        if (slotsJ)
        {
            int sz = json_array_size(slotsJ);
            for (int i=0;i<sz;++i)
            {
                auto sj = json_array_get(slotsJ,i);
                if (sj)
                {
                    std::string fn(json_string_value(sj));
                    if (i<bank.scales.size())
                    {
                        auto& temp = getScaleChecked(lastbindex,i);
                        if (temp.name!=fn)
                        {
                            KlangScale scale(fn);
                            if (scale.name.empty()==false)
                            {
                                bank.scales[i] = scale;
                                if (m_cur_bank == lastbindex && i == m_curScale)
                                {
                                    mScaleToChangeTo = scale.pitches;
                                    mDoScaleChange = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    std::string mCustomScaleFileName;
    void setFMAmount(float a)
    {
        a = clamp(a,0.0f,1.0f);
        m_fm_amt = std::pow(a,2.0f);
    }
    int m_warp_mode = 0;
    void setWarp(int mode,float w)
    {
        if (w!=m_warp || mode!=m_warp_mode)
        {
            w = clamp(w,0.0f,1.0f);
            m_warp = w;
            m_warp_mode = clamp(mode,0,2);
            for (int i=0;i<m_oscils.size();++i)
                m_oscils[i].setPhaseWarp(m_warp_mode,w);
        }
    }
    void setSpread(float s)
    {
        m_spread = clamp(s,0.0f,1.0f);
    }
    void setRootPitch(float p)
    {
        p = clamp(p,-36.0f,36.0f);
        m_root_pitch = p; 
    }
    void setPitchOffset(float p)
    {
        p = clamp(p,-36.0f,36.0f);
        m_freqratio = std::pow(dsp::FREQ_SEMITONE,p);
    }
    void setBalance(float b)
    {
        m_balance = clamp(b,0.0f,1.0f);
    }
    void setDetune(float d)
    {
        m_detune = clamp(d,0.0f,1.0f);
    }
    static const int chebyMorphCount = 8;
    alignas(16) float chebyMorphCoeffs[chebyMorphCount+1][16] =
    {
        {1.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0,0,0,0,0,0,0,0},
        {1.0f,1.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0,0,0,0,0,0,0,0},
        {1.0f,1.0f,1.0f,0.0f,0.0f,0.0f,0.0f,0.1f,0,0,0,0,0,0,0,0},
        {1.0f,1.0f,1.0f,1.0f,0.0f,0.0f,0.0f,0.0f,0.1,0,0,0,0,0,0,0},
        {1.0f,1.0f,1.0f,1.0f,1.0f,0.0f,0.0f,0.0f,0.0,0.1,0,0.0,0,0,0.1,0},
        {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,0.0f,0.0f,0,0.0,0,0.1,0,0,0.2,0},
        {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,0.0f,0,0.1,0,0,0.0,0,0.3,0},
        {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,0.0f,0,0.1,0,0,0,0,0.3,0}
    };
    OnePoleFilter mChebyMorphSmoother;
    float mChebyMorph = 0.0f;
    void setFold(float f)
    {
        f = clamp(f,0.0f,1.0f);
        mChebyMorph = f;
        
        m_fold = std::pow(f,3.0f);
    }
    void setOscCount(int c)
    {
        m_active_oscils = clamp(c,1,16);
    }
    int getOscCount()
    {
        return m_active_oscils;
    }
    void setFMAlgo(int m)
    {
        m_fm_algo = clamp(m,0,2);
    }
    void setFMMode(int m)
    {
        m_fm_mod_mode = clamp(m,0,2);
    }
    void setSpreadDistribution(float x)
    {
        m_spread_dist = clamp(x,0.0f,1.0f);
    }
    void setFrequencySmoothing(float s)
    {
        if (s!=m_freq_smooth)
        {
            float shaped = 1.0f-std::pow(1.0f-s,3.0f);
            shaped = rescale(shaped,0.0f,1.0f,0.99f,0.99999f);
            for (int i=0;i<m_osc_freq_smoothers.size();++i)
                m_osc_freq_smoothers[i].setAmount(shaped);
            m_freq_smooth = s;
        }
    }
    void setXFadeMode(int m)
    {
        mXFadeMode = m;
    }
    void setFreezeEnabled(bool b)
    {
        // ignore first set so that the oscillator frequencies will be updated at start
        if (mFreezeRunCount>0)
        {
            mFreeze_enabled = b;
        }
        ++mFreezeRunCount;
    }
    bool freezeEnabled() const { return mFreeze_enabled; }
    void setFreezeMode(int m)
    {
        mFreeze_mode = m;
    }
    void setScaleBank(int b)
    {
        b = clamp(b,0,m_all_banks.size()-1);
        if (m_cur_bank!=b)
        {
            m_cur_bank = b;
            m_scale = getScaleChecked(m_cur_bank,m_curScale).pitches;
        }
        
    }
    void setFoldAlgo(int a)
    {
        m_fold_algo = clamp(a,0,2);
    }
    OnePoleFilter m_norm_smoother;
    std::array<float,32> m_osc_gains;
    std::array<float,32> m_osc_freqs;
    std::array<float,32> m_unquant_freqs;
private:
    alignas(16) std::array<SIMDSimpleOsc,16> m_oscils;
    
    
    std::array<OnePoleFilter,32> m_osc_gain_smoothers;
    std::array<OnePoleFilter,32> m_osc_freq_smoothers;
    
    alignas(16) QuadFilterWaveshaperState mShaperStates[16];    

    OnePoleFilter m_fold_smoother;
    std::vector<double> m_scale;
    float m_spread = 1.0f;
    float m_root_pitch = 0.0f;
    float m_freqratio = 1.0f;
    float m_balance = 0.0f;
    float m_detune = 0.1;
    float m_fold = 0.0f;
    float m_fm_amt = 0.0f;
    int m_active_oscils = 16;
    float m_warp = 1.1f;    
    int m_fm_algo = 0;
    int m_fold_algo = 0;
    float m_freq_smooth = -1.0f;
    float m_spread_dist = 0.5f;
    int mXFadeMode = 2;
    bool mFreeze_enabled = false;
    int mFreeze_mode = 0;
    int m_fm_mod_mode = 0;
    std::vector<KlangScaleBank> m_all_banks;
    int m_cur_bank = 0;
    int mFreezeRunCount = 0;
    spinlock m_lock;
    std::atomic<bool> mDoScaleChange{false};
    std::vector<double> mScaleToChangeTo;
    std::array<float,4096> mExpFMPowerTable;
};

#ifndef RAPIHEADLESS

class XScaleOsc : public Module
{
public:
    enum OUTPUTS
    {
        OUT_AUDIO_1,
        OUT_LAST
    };
    enum INPUTS
    {
        IN_ROOT,
        IN_PITCH,
        IN_BALANCE,
        IN_SPREAD,
        IN_FOLD,
        IN_DETUNE,
        IN_NUM_OSCS,
        IN_FM_AMT,
        IN_WARP,
        IN_SCALE,
        IN_FREEZE,
        IN_LAST
    };
    enum PARAMETERS
    {
        PAR_ROOT,
        PAR_PITCH_OFFS,
        PAR_BALANCE,
        PAR_DETUNE,
        PAR_NUM_OSCS,
        PAR_FOLD,
        PAR_SPREAD,
        PAR_WARP,
        PAR_FM_AMT,
        PAR_FM_ALGO,
        PAR_SCALE,
        PAR_SCALE_BANK,
        PAR_WARP_MODE,
        PAR_NUM_OUTPUTS,
        PAR_FREQSMOOTH,
        PAR_SPREAD_DIST,
        PAR_ROOT_ATTN,
        PAR_BAL_ATTN,
        PAR_PITCH_ATTN,
        PAR_SPREAD_ATTN,
        PAR_FM_ATTN,
        PAR_SCALE_ATTN,
        PAR_DETUNE_ATTN,
        PAR_FOLD_ATTN,
        PAR_WARP_ATTN,
        PAR_XFADEMODE,
        PAR_FREEZE_ENABLED,
        PAR_FREEZE_MODE,
        PAR_FM_MODE,
        PAR_FOLD_MODE,
        PAR_HIPASSFREQ,
        PAR_LAST
    };
    XScaleOsc()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_BALANCE,0.0f,1.0f,0.4f,"Balance");
        configParam(PAR_ROOT,-36.0f,36.0f,0.0f,"Root");
        configParam(PAR_PITCH_OFFS,-36.0f,36.0f,0.0f,"Pitch offset");
        configParam(PAR_DETUNE,0.0f,1.0f,0.0f,"Detune");
        configParam(PAR_NUM_OSCS,1.0f,16.0f,16.0f,"Num oscillators");
        getParamQuantity(PAR_NUM_OSCS)->snapEnabled = true;
        configParam(PAR_FOLD,0.0f,1.0f,0.0f,"Fold");
        configParam(PAR_SPREAD,0.0f,1.0f,0.5f,"Spread");
        configParam(PAR_WARP,0.0f,1.0f,0.0f,"Warp");
        configParam(PAR_FM_AMT,0.0f,1.0f,0.0f,"FM Amount");
        configParam(PAR_FM_ALGO,0.0f,2.0f,0.0f,"FM Algorithm");
        getParamQuantity(PAR_FM_ALGO)->snapEnabled = true;
        configParam(PAR_SCALE,0.0f,1.0f,0.0f,"Scale");
        
        configParam(PAR_WARP_MODE,0.0f,2.0f,0.0f,"Warp Mode");
        getParamQuantity(PAR_WARP_MODE)->snapEnabled = true;
        configParam(PAR_NUM_OUTPUTS,1.0f,16.0f,1.0f,"Num outputs");
        getParamQuantity(PAR_NUM_OUTPUTS)->snapEnabled = true;
        configParam(PAR_FREQSMOOTH,0.0f,1.0f,0.1f,"Pitch smoothing");
        configParam(PAR_SPREAD_DIST,0.0f,1.0f,0.5f,"Spread distribution");
        
        configParam(PAR_SPREAD_ATTN,-1.0f,1.0f,0.0f,"Spread CV level");
        configParam(PAR_ROOT_ATTN,-1.0f,1.0f,1.0f,"Root CV level");
        configParam(PAR_BAL_ATTN,-1.0f,1.0f,0.0f,"Balance CV level");
        configParam(PAR_PITCH_ATTN,-1.0f,1.0f,1.0f,"Pitch CV level");
        configParam(PAR_FM_ATTN,-1.0f,1.0f,0.0f,"FM CV level");
        configParam(PAR_SCALE_ATTN,-1.0f,1.0f,0.0f,"Scale CV level");
        configParam(PAR_FOLD_ATTN,-1.0f,1.0f,0.0f,"Fold CV level");
        configParam(PAR_WARP_ATTN,-1.0f,1.0f,0.0f,"Warp CV level");
        configParam(PAR_DETUNE_ATTN,-1.0f,1.0f,0.0f,"Detune CV level");

        configParam(PAR_XFADEMODE,0.0f,3.0f,1.0f,"Crossfade mode");
        getParamQuantity(PAR_XFADEMODE)->snapEnabled = true;
        
        configSwitch(PAR_FREEZE_ENABLED,0.0f,1.0f,0.0f,"Freeze frequencies",{"Off","On"});
        configSwitch(PAR_FREEZE_MODE,0.0f,2.0f,0.0f,"Freeze mode",
            {"Odd oscillators","Bottom oscillators","Lowest oscillator"});
        configParam(PAR_FM_MODE,0.0f,2.0f,0.0f,"FM Mode"); 
        getParamQuantity(PAR_FM_MODE)->snapEnabled = true;
        
        configParam(PAR_SCALE_BANK,0,m_osc.getNumBanks()-1,0,"Scale bank");
        getParamQuantity(PAR_SCALE_BANK)->snapEnabled = true;
        configParam(PAR_FOLD_MODE,0,2,0,"Fold mode");
        getParamQuantity(PAR_FOLD_MODE)->snapEnabled = true;
        configParam(PAR_HIPASSFREQ,10.0f,200.0f,10.0f,"Low cut filter frequency");
        m_pardiv.setDivision(16);
    }
    float m_samplerate = 0.0f;
    inline float getModParValue(int parId, int inId, int attnId=-1, bool doClamp=false, float clampMin = 0.0f, float clampMax = 0.0f)
    {
        float p = params[parId].getValue();
        if (attnId>=0)
            p += inputs[inId].getVoltage()*0.1f*params[attnId].getValue();
        else
            p += inputs[inId].getVoltage()*0.1f;
        if (doClamp)
            p = clamp(p,clampMin,clampMax);
        return p;
    } 

    float mLastHPCutoff = 0.0f;
    void process(const ProcessArgs& args) override
    {
        float hphz = params[PAR_HIPASSFREQ].getValue();
        if (m_samplerate!=args.sampleRate || mLastHPCutoff!=hphz)
        {
            // we don't want to calculate the coeffs for all filter instances, because they are the same
            float normfreq = hphz/args.sampleRate;
            float q = sqrt(2.0)/2.0;
            m_hpfilts[0].setParameters(rack::dsp::BiquadFilter::HIGHPASS,normfreq,q,1.0f);
            for (int i=1;i<16;++i)
            {
                m_hpfilts[i].a[0] = m_hpfilts[0].a[0];
                m_hpfilts[i].a[1] = m_hpfilts[0].a[1];
                m_hpfilts[i].b[0] = m_hpfilts[0].b[0];
                m_hpfilts[i].b[1] = m_hpfilts[0].b[1];
                m_hpfilts[i].b[2] = m_hpfilts[0].b[2];
            }
            m_samplerate = args.sampleRate;
            mLastHPCutoff = hphz;
        }
        bool tempFz = m_freezeTrigger.process(inputs[IN_FREEZE].getVoltage(),0.0f,10.0f);
        if (tempFz == true) 
        {
            
            if (params[PAR_FREEZE_ENABLED].getValue()>0.0f)
                params[PAR_FREEZE_ENABLED].setValue(0.0f);
            else params[PAR_FREEZE_ENABLED].setValue(1.0f);
        }
        if (m_pardiv.process())
        {
            float bal = params[PAR_BALANCE].getValue(); 
            bal += inputs[IN_BALANCE].getVoltage()*0.1f*averterMap(params[PAR_BAL_ATTN].getValue(),0.0f);
            m_osc.setBalance(bal);
            float detune = getModParValue(PAR_DETUNE,IN_DETUNE,PAR_DETUNE_ATTN);
            m_osc.setDetune(detune);
            int foldmode = params[PAR_FOLD_MODE].getValue();
            m_osc.setFoldAlgo(foldmode);
            float fold = getModParValue(PAR_FOLD,IN_FOLD,PAR_FOLD_ATTN);
            m_osc.setFold(fold);
            float pitch = params[PAR_PITCH_OFFS].getValue();
            pitch += inputs[IN_PITCH].getVoltage()*12.0f*params[PAR_PITCH_ATTN].getValue();
            pitch = clamp(pitch,-48.0f,48.0);
            m_osc.setPitchOffset(pitch);
            float root = params[PAR_ROOT].getValue();
            float tempa = params[PAR_ROOT_ATTN].getValue();
            if (tempa>=0.0f)
            {
                tempa = std::pow(tempa,2.0f);
            } else
            {
                tempa = -std::pow(tempa,2.0f);
            }
            root += inputs[IN_ROOT].getVoltage()*12.0f*tempa;
            m_osc.setRootPitch(root);
            float osccount = params[PAR_NUM_OSCS].getValue();
            osccount += inputs[IN_NUM_OSCS].getVoltage() * (16.0f/10.0f);
            m_osc.setOscCount(osccount);
            float spread = getModParValue(PAR_SPREAD,IN_SPREAD,PAR_SPREAD_ATTN);
            m_osc.setSpread(spread);
            float sdist = params[PAR_SPREAD_DIST].getValue();
            m_osc.setSpreadDistribution(sdist);
            
            float warp = getModParValue(PAR_WARP,IN_WARP,PAR_WARP_ATTN);
            int wmode = params[PAR_WARP_MODE].getValue();
            m_osc.setWarp(wmode,warp);
            float fm = getModParValue(PAR_FM_AMT,IN_FM_AMT,PAR_FM_ATTN);
            m_osc.setFMAmount(fm);
            int fmalgo = params[PAR_FM_ALGO].getValue();
            m_osc.setFMAlgo(fmalgo);
            int fmmode = params[PAR_FM_MODE].getValue();
            m_osc.setFMMode(fmmode);
            int bank = params[PAR_SCALE_BANK].getValue();
            m_osc.setScaleBank(bank);
            float scale = getModParValue(PAR_SCALE,IN_SCALE,PAR_SCALE_ATTN);
            m_osc.setScale(scale);
            float psmooth = params[PAR_FREQSMOOTH].getValue();
            m_osc.setFrequencySmoothing(psmooth);
            int xfmode = params[PAR_XFADEMODE].getValue();
            m_osc.setXFadeMode(xfmode);
            
            m_osc.setFreezeEnabled((bool)params[PAR_FREEZE_ENABLED].getValue());
            int freezeMode = params[PAR_FREEZE_MODE].getValue();
            m_osc.setFreezeMode(freezeMode);
            
            m_osc.updateOscFrequencies();
        }
        alignas(16) float outs[16];
        m_osc.processNextFrame(outs,args.sampleRate);
        int numOutputs = params[PAR_NUM_OUTPUTS].getValue();
        int numOscs = m_osc.getOscCount();
        outputs[OUT_AUDIO_1].setChannels(numOutputs);
        
        float mixed[16];
        for (int i=0;i<numOutputs;++i)
            mixed[i] = 0.0f;
        for (int i=0;i<16;++i)
        {
            int outindex = i % numOutputs;
            mixed[outindex] += outs[i];
            
        }
        float nnosc = rescale((float)numOscs,1,16,0.0f,1.0f);
        float normscaler = 0.25f+0.75f*std::pow(1.0f-nnosc,3.0f);
        float outgain = m_osc.m_norm_smoother.process(normscaler);
        for (int i=0;i<numOutputs;++i)
        {
            float outsample = mixed[i]*5.0f*outgain;
            outsample = m_hpfilts[i].process(outsample);
            outputs[OUT_AUDIO_1].setVoltage(outsample,i);
        }
    }
    json_t* dataToJson() override
    {
        json_t* resultJ = json_object();
        auto ob = m_osc.dataToJson();
        if (ob)
        {
            json_object_set(resultJ,"osccustomdata0",ob);
        }
        json_object_set(resultJ,"directscale",json_integer(m_osc.m_pitchQuantizeMode));
        return resultJ;
    }
    void dataFromJson(json_t* root) override
    {
        if (auto ob = json_object_get(root,"osccustomdata0")) m_osc.dataFromJson(ob);
        if (auto ij = json_object_get(root,"directscale")) m_osc.m_pitchQuantizeMode = json_integer_value(ij);
    }
    
    alignas(16) ScaleOscillator m_osc;
    dsp::ClockDivider m_pardiv;
    dsp::TBiquadFilter<float> m_hpfilts[16];
    dsp::SchmittTrigger m_freezeTrigger;
};

class MyLatchButton : public VCVButton
{
public:
    MyLatchButton() : VCVButton()
    {
        this->latch = true;
        this->momentary = false;
    }
};

struct MyLoadFileItem : MenuItem
{
    XScaleOsc* m_mod = nullptr;
    void onAction(const event::Action &e) override
    {
        std::string dir; 
        osdialog_filters* filters = osdialog_filters_parse("Scala File:scl");
        char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
        osdialog_filters_free(filters);
        if (!pathC) {
            return;
        }
        std::string path = pathC;
        std::free(pathC);
        m_mod->m_osc.loadScaleFromFile(path);
    }
};

class XScaleOscWidget : public ModuleWidget
{
public:
    void appendContextMenu(Menu *menu) override 
    {
		auto loadItem = createMenuItem<MyLoadFileItem>("Import .scl (Scala) file...");
		XScaleOsc* themod = dynamic_cast<XScaleOsc*>(module);
        loadItem->m_mod = themod;
		menu->addChild(loadItem);
        auto chebyItem = createMenuItem([themod]()
        {
            themod->m_osc.refreshChebyCoeffs();
        },"Update Chebyshev coeffs");
        menu->addChild(chebyItem);
        bool tick = themod->m_osc.m_pitchQuantizeMode == 1;
        auto quantitem = createMenuItem([themod]()
        {
            if (themod->m_osc.m_pitchQuantizeMode == 0)
                themod->m_osc.m_pitchQuantizeMode = 1;
            else themod->m_osc.m_pitchQuantizeMode = 0;
        },"Use scale steps directly",CHECKMARK(tick));
        menu->addChild(quantitem);
    }
    XScaleOscWidget(XScaleOsc* m)
    {
        setModule(m);
        box.size.x = RACK_GRID_WIDTH * 23;
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "KLANG",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        auto port = new PortWithBackGround(m,this,XScaleOsc::OUT_AUDIO_1,1,30,"AUDIO OUT 1",true);
        float xc = 1.0f;
        float yc = 80.0f;
        KnobInAttnWidget* kwid = nullptr;
        auto defrand = [](){ return random::uniform(); };
        addChild(kwid = new KnobInAttnWidget(this,"ROOT",XScaleOsc::PAR_ROOT,
            XScaleOsc::IN_ROOT,XScaleOsc::PAR_ROOT_ATTN,xc,yc));
        kwid->m_knob->GetRandomizedValue = defrand;
        xc+=82.0f;
        addChild(kwid = new KnobInAttnWidget(this,"BALANCE",XScaleOsc::PAR_BALANCE,
            XScaleOsc::IN_BALANCE,XScaleOsc::PAR_BAL_ATTN,xc,yc));
        kwid->m_knob->GetRandomizedValue = defrand;
        xc+=82.0f;
        addChild(new KnobInAttnWidget(this,"PITCH",XScaleOsc::PAR_PITCH_OFFS,
            XScaleOsc::IN_PITCH,XScaleOsc::PAR_PITCH_ATTN,xc,yc));
        xc+=82.0f;
        addChild(kwid = new KnobInAttnWidget(this,"SPREAD",XScaleOsc::PAR_SPREAD,
            XScaleOsc::IN_SPREAD,XScaleOsc::PAR_SPREAD_ATTN,xc,yc));
        kwid->m_knob->GetRandomizedValue = defrand;
        xc = 1.0f;
        yc += 47.0f;
        addChild(kwid = new KnobInAttnWidget(this,"DETUNE",XScaleOsc::PAR_DETUNE,
            XScaleOsc::IN_DETUNE,XScaleOsc::PAR_DETUNE_ATTN,xc,yc));
        kwid->m_knob->GetRandomizedValue = defrand;
        xc += 82.0f;
        addChild(kwid = new KnobInAttnWidget(this,"FOLD",XScaleOsc::PAR_FOLD,
            XScaleOsc::IN_FOLD,XScaleOsc::PAR_FOLD_ATTN,xc,yc));
        kwid->m_knob->GetRandomizedValue = defrand;
        addParam(createParam<Trimpot>(Vec(xc+57.f, yc+0.0f), module, XScaleOsc::PAR_FOLD_MODE));
        xc += 82.0f;
        
        addChild(new KnobInAttnWidget(this,"NUM OSCS",XScaleOsc::PAR_NUM_OSCS,
            XScaleOsc::IN_NUM_OSCS,-1,xc,yc,true));
        
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"WARP",XScaleOsc::PAR_WARP,
            XScaleOsc::IN_WARP,XScaleOsc::PAR_WARP_ATTN,xc,yc));
        xc = 1.0f;
        yc += 47.0f;
        addChild(kwid = new KnobInAttnWidget(this,"FM AMOUNT",XScaleOsc::PAR_FM_AMT,
            XScaleOsc::IN_FM_AMT,XScaleOsc::PAR_FM_ATTN,xc,yc));
        kwid->m_knob->GetRandomizedValue = defrand;
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"FM ALGORITHM",XScaleOsc::PAR_FM_ALGO,
            -1,-1,xc,yc,true));
        addParam(createParam<Trimpot>(Vec(xc+31.0f, yc+19.0f), module, XScaleOsc::PAR_FM_MODE));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"SCALE",XScaleOsc::PAR_SCALE,
            XScaleOsc::IN_SCALE,XScaleOsc::PAR_SCALE_ATTN,xc,yc));
        addParam(createParam<Trimpot>(Vec(xc+57.0f, yc+0.0f), module, XScaleOsc::PAR_SCALE_BANK));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"WARP MODE",XScaleOsc::PAR_WARP_MODE,
            -1,-1,xc,yc,true));
        xc = 1.0f;
        yc += 47.0f;
        addChild(new KnobInAttnWidget(this,"NUM OUTPUTS",XScaleOsc::PAR_NUM_OUTPUTS,
            -1,-1,xc,yc,true));
        addParam(createParam<Trimpot>(Vec(xc+31.0f, yc+19.0f), module, XScaleOsc::PAR_HIPASSFREQ));
        xc += 82.0f;
        addChild(new KnobInAttnWidget(this,"PITCH SMOOTHING",XScaleOsc::PAR_FREQSMOOTH,
            -1,-1,xc,yc,false));
        xc += 82.0f;
        addChild(kwid = new KnobInAttnWidget(this,"SPREAD DISTR",XScaleOsc::PAR_SPREAD_DIST,
            -1,-1,xc,yc,false));
        xc += 82.0f;
        addChild(kwid = new KnobInAttnWidget(this,"XFADE MODE",XScaleOsc::PAR_XFADEMODE,
            -1,-1,xc,yc,false));
        myoffs = yc+45.0f; // kwid->box.pos.y+kwid->box.size.y;
        addParam(createParam<CKSS>(Vec(35.0, 32.0), module, XScaleOsc::PAR_FREEZE_ENABLED));
        addInput(createInput<PJ301MPort>(Vec(35.0f, 55.0f), module, XScaleOsc::IN_FREEZE));
        addParam(createParam<CKSSThree>(Vec(64.0, 32.0), module, XScaleOsc::PAR_FREEZE_MODE));
    }
    
    float myoffs = 0.0f;
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x50, 0x50, 0x50, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);
        XScaleOsc* m = dynamic_cast<XScaleOsc*>(module);
        if (m)
        {
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
            nvgRect(args.vg,0.0f,myoffs,w,80.0f);
            nvgFill(args.vg);
            for (int j=0;j<2;++j)
            {
                nvgBeginPath(args.vg);
                nvgStrokeWidth(args.vg,3.0f);
                if (j == 0)
                    nvgStrokeColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 200));
                else
                    nvgStrokeColor(args.vg, nvgRGBA(0xff, 0x00, 0x00, 200));
                for (int i=0;i<m->m_osc.getOscCount();++i)
                {
                    float hz = m->m_osc.m_osc_freqs[i*2+j];
                    float pitch = 12.0f * log2f(hz/(dsp::FREQ_C4/16.0f));
                    float gain = m->m_osc.m_osc_gains[i*2+j];
                    float xcor = rescale(pitch,0.0f,120.0f,1.0f,box.size.x-1);
                    xcor = clamp(xcor,0.0f,box.size.x);
                    float ybase = myoffs;// + (i % 2) * 40.0f;
                    
                    nvgMoveTo(args.vg,xcor,ybase+80.0f-(80.0*gain));
                    nvgLineTo(args.vg,xcor,ybase+80.0f);
                }
                nvgStroke(args.vg);
            }
            auto scalename = rack::system::getFilename(m->m_osc.getScaleName());
            nvgFontSize(args.vg, 18);
            nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            nvgText(args.vg,1.0f,myoffs+14,scalename.c_str(),nullptr);
            nvgBeginPath(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            for (int i=0;i<16;++i)
            {
                float y = m->m_osc.mChebyCoeffs[i];
                y = rescale(y,0.0f,1.0f,y+50,y);
                nvgMoveTo(args.vg,200+i*2,y);
                nvgLineTo(args.vg,200+i*2,50);
            }
            nvgStroke(args.vg);
        }
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelXScaleOscillator = createModel<XScaleOsc, XScaleOscWidget>("XScaleOscillator");

#else

int main(int argc, char** argv)
{
    std::cout << "Starting headless KLANG\n";
    return 0;
}
#endif
