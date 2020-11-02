#include "plugin.hpp"
#include <random>
#include <stb_image.h>


extern std::shared_ptr<Font> g_font;

/*
voice minpitch maxpitch pitch maxdur gate  par1  par2  par3  par4
1     24.0     48.0     [out] 3.0    [out] [out] [out] [out] [out]


ppp pp p mp mf f ff 
  1  2 3  4  5 6  7

*/

const int EnvelopeTable[44][4] =
{
    {1,1,0,0}, // ppp
    {2,1,2,0}, // ppp < p
    {3,1,2,1}, // ppp < p > ppp
    {2,2,1,0}, // p > ppp
    {2,1,3,0}, // p < f
    {3,1,3,1}, // ppp < f > ppp
};

/*
const float EnvelopeScalers[2][4] =
{
    {1.0f/7*1,1.0f/7*3,1.0f/7*6,1.0f},
    {0.0,1.0f/3*1,1.0f/3*2,1.0f}
};
*/

class STEnvelope
{
public:
    void start(int envIndex, float sampleRate, float length, bool attackReleaseFades)
    {
        mActiveEnvelope = envIndex;
        mSampleRate = sampleRate;
        mLen = length;
        mSamplePos = 0;
        mEnvIndex = 0;
        mNumEnvPoints = EnvelopeTable[envIndex][0];
        mFadesActive = attackReleaseFades;
    }
    float process()
    {
        if (mActiveEnvelope == -1)
            return 0.0f;
        float envValue = 0.0f;
        if (mNumEnvPoints == 1)
        {
            envValue = EnvelopeTable[mActiveEnvelope][1];
        }
        int envLenSamples = mSampleRate * mLen;
        if (mFadesActive)
        {
            float fadeGain = 1.0f;
            int fadeLenSamples = mSampleRate * mFadeLen;
            if (mSamplePos<fadeLenSamples)
                fadeGain = rescale(mSamplePos,0,fadeLenSamples,0.0f,1.0f);
            if (mSamplePos>envLenSamples-fadeLenSamples)
                fadeGain = rescale(mSamplePos,envLenSamples-fadeLenSamples,envLenSamples,1.0f,0.0f);
            envValue *= fadeGain;
        }
        return envValue;
    }
private:
    int mActiveEnvelope = -1;
    int mNumEnvPoints = 0;
    float mSampleRate = 0.0f;
    float mLen = 0.0f;
    bool mFadesActive = false;
    int mSamplePos = 0;
    int mEnvIndex = 0;
    float mFadeLen = 0.01;
};

class XStochastic : public rack::Module
{
public:
    XStochastic()
    {

    }
};

class XStochasticWidget : public ModuleWidget
{
public:
    
    XStochasticWidget(XStochastic* m)
    {
        setModule(m);
        box.size.x = 600.0f;
        if (!g_font)
        	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
        
    }
    ~XStochasticWidget()
    {
        
    }
    void draw(const DrawArgs &args) override
    {
        
        ModuleWidget::draw(args);
    }
private:
    
};

Model* modelXStochastic = createModel<XStochastic, XStochasticWidget>("XStochastic");
