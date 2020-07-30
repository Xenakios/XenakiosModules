#include "plugin.hpp"
#include <random>

extern std::shared_ptr<Font> g_font;

template<typename T>
inline double grid_value(const T& ge)
{
    return ge;
}


#define VAL_QUAN_NORILO

template<typename T,typename Grid>
inline double quantize_to_grid(T x, const Grid& g, double amount=1.0)
{
    auto t1=std::lower_bound(std::begin(g),std::end(g),x);
    if (t1!=std::end(g))
    {
        /*
        auto t0=t1-1;
        if (t0<std::begin(g))
            t0=std::begin(g);
        */
        auto t0=std::begin(g);
        if (t1>std::begin(g))
            t0=t1-1;
#ifndef VAL_QUAN_NORILO
        const T half_diff=(*t1-*t0)/2;
        const T mid_point=*t0+half_diff;
        if (x<mid_point)
        {
            const T diff=*t0-x;
            return x+diff*amount;
        } else
        {
            const T diff=*t1-x;
            return x+diff*amount;
        }
#else
        const double gridvalue = fabs(grid_value(*t0) - grid_value(x)) < fabs(grid_value(*t1) - grid_value(x)) ? grid_value(*t0) : grid_value(*t1);
        return x + amount * (gridvalue - x);
#endif
    }
    auto last = std::end(g)-1;
    const double diff=grid_value(*(last))-grid_value(x);
    return x+diff*amount;
}


class Quantizer
{
public:
    Quantizer()
    {
        std::mt19937 gen;
        std::uniform_real_distribution<float> dist(-10.0,10.0f);
        //for (int i=0;i<7;++i)
        //    voltages.push_back(dist(gen));
        //std::sort(voltages.begin(),voltages.end());
        voltages = {-7.1f,-5.0f,-2.5f,0.0f,2.5f,5.0f,6.66f,8.25f};
    }
    float process(float x, float strength)
    {
        return quantize_to_grid(x,voltages,strength);

        auto it = std::lower_bound(voltages.begin(),voltages.end(),x);
        //if (it == voltages.end())
        //    return x;
        --it;
        float q0 = *it;
        ++it;
        if (it == voltages.end())
            --it;
        float q1 = *it;
        float d0 = fabs(q0-x);
        float d1 = fabs(q1-x);
        if (d0<d1)
            return q0;
        return q1;
    }
private:
    std::vector<float> voltages;
};

class XQuantModule : public rack::Module
{
public:
    enum InputPorts
    {
        FIRSTINPUT = 0,
        LASTINPUT = 7
    };
    enum OutputPorts
    {
        FIRSTOUTPUT = 0,
        LASTOUTPUT = FIRSTOUTPUT+7
    };
    std::vector<float> heldOutputs;
    dsp::ClockDivider divider;
    XQuantModule()
    {
        divider.setDivision(16);
        heldOutputs.resize(8);
        config(1,8,8,0);
        configParam(0,0.0f,1.0f,0.05f,"Foopar");
    }
    void process(const ProcessArgs& args) override
    {
        if (divider.process())
        {
            float strength = params[0].getValue();
            for (int i=0;i<8;++i)
            {
                if (outputs[i].isConnected())
                    heldOutputs[i] = quantizers[i].process(inputs[i].getVoltage(),strength);
            }
        }
        for (int i=0;i<8;++i)
        {
            outputs[i].setVoltage(heldOutputs[i]);
        }
        
    }
    Quantizer quantizers[8];
};

class XQuantWidget : public ModuleWidget
{
public:
    XQuantWidget(XQuantModule* m)
    {
        if (!g_font)
        	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
        setModule(m);
        box.size.x = 400;
        for (int i=0;i<8;++i)
        {
            addInput(createInputCentered<PJ301MPort>(Vec(30, 30+i*30), m, XQuantModule::FIRSTINPUT+i));
            addOutput(createOutputCentered<PJ301MPort>(Vec(370, 30+i*30), m, XQuantModule::FIRSTOUTPUT+i));
        }
        addParam(createParam<RoundLargeBlackKnob>(Vec(38, 250), module, 0));
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
        nvgText(args.vg, 3 , 10, "XQuantizerA", NULL);
        nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }

};

Model* modelXQuantizer = createModel<XQuantModule, XQuantWidget>("XQuantizer");