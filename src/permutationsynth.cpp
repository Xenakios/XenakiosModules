#include "plugin.hpp"
#include <random>
#include "wdl/resample.h"

template<typename T>
inline T reflect_value(const T& minval, const T& val, const T& maxval)
{
    T temp = val;
    while (temp<minval || temp>maxval)
    {
        if (temp < minval)
            temp = minval + (minval - temp);
        if (temp > maxval)
            temp = maxval + (maxval - temp);
    }
    return temp;
}

struct WaveSegment
{
    WaveSegment() {}
    template<typename Func>
    WaveSegment(int size, Func f)
    {
        data.resize(size);
        for (int i = 0; i < size; ++i)
            data[i] = f(rescale(i, 0, size - 1, 0.0, 1.0));
    }
    std::vector<float> data;

};

std::vector<int> UpTo(int n, int offset = 0)
{
    std::vector<int> retval(n);
    for (int ii = 0; ii < n; ++ii)
        retval[ii] = ii + offset;
    return retval;
}

struct JohnsonTrotterState_
{
    std::vector<int> values_;
    std::vector<int> positions_;    // size is n+1, first element is not used
    std::vector<char> directions_;
    int sign_;

    JohnsonTrotterState_(int n) : values_(UpTo(n, 1)), positions_(UpTo(n + 1, -1)), directions_(n + 1, 0), sign_(1) {}
    void reset(int n)
    {
        values_ = UpTo(n, 1);
        positions_ = UpTo(n + 1, -1);
        directions_.resize(n + 1);
        std::fill(directions_.begin(), directions_.end(), 0);
        sign_ = 1;
    }
    int LargestMobile() const    // returns 0 if no mobile integer exists
    {
        for (int r = (int)values_.size(); r > 0; --r)
        {
            const int loc = positions_[r] + (directions_[r] ? 1 : -1);
            if (loc >= 0 && loc < values_.size() && values_[loc] < r)
                return r;
        }
        return 0;
    }

    bool IsComplete() const { return LargestMobile() == 0; }

    void operator++()    // implement Johnson-Trotter algorithm
    {
        const int r = LargestMobile();
        const int rLoc = positions_[r];
        const int lLoc = rLoc + (directions_[r] ? 1 : -1);
        const int l = values_[lLoc];
        // do the swap
        std::swap(values_[lLoc], values_[rLoc]);
        std::swap(positions_[l], positions_[r]);
        sign_ = -sign_;
        // change directions
        for (auto pd = directions_.begin() + r + 1; pd != directions_.end(); ++pd)
            *pd = !*pd;
    }
};

const float pi = 3.141592653;

class PermutationOscillator
{
public:
    PermutationOscillator()
    {
        std::mt19937 gen;
        std::uniform_real_distribution<float> dist(-1.0, 1.0);
        
        segments.emplace_back(150, [](float x) { return 0.0f; });
        segments.emplace_back(64, [](float x) { return sin(x * pi); });
        segments.emplace_back(64, [](float x) { return cos(x * pi / 2.0); });
        segments.emplace_back(64, [](float x) { return -1.0 + 2.0 * x; });
        segments.emplace_back(7, [](float x) { return -x; });
        segments.emplace_back(128, [](float x) { return x; });
        segments.emplace_back(2000, [](float x) { return sin(x * pi * 2.0); });
        segments.emplace_back(64, [&gen, &dist](float x) { return dist(gen); });
        segments.emplace_back(31, [](float x) { return fmod(x * 5.0, 1.0); });
        segments.emplace_back(300, [](float x) { return sin(x * pi * 2) * sin(x * pi * 15.13); });
        segments.emplace_back(400, [](float x) { return sin(x * pi * 13) * sin(x * pi * 15.13); });
        segments.emplace_back(150, [&gen, &dist](float x) { return sin(x * pi) * dist(gen); });
        rsOutBuf.resize(4);
    }
    float process(float srate, float pitch)
    {
        float ratio = pow(2.0,(pitch/12.0));
        rs.SetRates(ratio*srate, srate);
        double* rsInBuf = nullptr;
        int wanted = rs.ResamplePrepare(1, 1, &rsInBuf);
        float reflGain = 1.0;
        for (int k = 0; k < wanted; ++k)
        {
            WaveSegment& seg = segments[jtstate.values_[elementCounter] - 1];
            float sample = seg.data[segmentCounter];
            sample = reflect_value(-1.0f, sample * reflGain, 1.0f);
            rsInBuf[k] = sample * 0.25;
            //buffer.setSample(0, k, sample*0.25);
            ++segmentCounter;
            if (segmentCounter >= seg.data.size())
            {
                segmentCounter = 0;
                ++elementCounter;
                if (elementCounter == jtstate.values_.size())
                {
                    elementCounter = 0;
                    ++jtstate;
                    if (jtstate.IsComplete())
                    {
                        jtstate.reset(numElements);
                    }
                }
            }
        }
        rs.ResampleOut(rsOutBuf.data(), wanted, 1, 1);
        return rsOutBuf[0];
    }
private:
    WDL_Resampler rs;
    JohnsonTrotterState_ jtstate{12};
    std::vector<WaveSegment> segments;
    int elementCounter = 0;
    int segmentCounter = 0;
    int numElements = 12;
    std::vector<double> rsOutBuf;
};

class XPSynth : public rack::Module
{
public:
    enum PARAMS
    {
        FREQ_PARAM
    };
    XPSynth()
    {
        config(1,1,1);
        configParam(FREQ_PARAM, -48.f, 48.f, 0.f, "Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
    }
    void process(const ProcessArgs& args) override
    {
        float pitch = params[FREQ_PARAM].getValue();
        pitch += inputs[0].getVoltage()*12.0;
        pitch = clamp(pitch,-48.0,48.0);
        float sample = osc.process(args.sampleRate,pitch);
        outputs[0].setVoltage(sample*5.0f);
    }
private:
    PermutationOscillator osc;
};

extern std::shared_ptr<Font> g_font;

class XPSynthWidget : public ModuleWidget
{
public:
    XPSynthWidget(XPSynth* m)
    {
        setModule(m);
        box.size.x = 100;
        if (!g_font)
            g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
        addOutput(createOutputCentered<PJ301MPort>(Vec(35, 30), m, 0));
        addInput(createInputCentered<PJ301MPort>(Vec(65, 60), m, 0));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(35, 60), m, XPSynth::FREQ_PARAM));
    }
    void draw(const DrawArgs &args)
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x40, 0x40, 0x40, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);

        nvgFontSize(args.vg, 15);
        nvgFontFaceId(args.vg, g_font->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        
        nvgText(args.vg, 3 , 10, "XPSynth", NULL);
        nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }    
};

Model* modelXPSynth = createModel<XPSynth, XPSynthWidget>("XPermutationSynth");
