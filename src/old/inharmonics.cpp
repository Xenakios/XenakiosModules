#include "../plugin.hpp"

inline bool fuzzyFind(std::vector<double>& v, double x)
{
    for (auto& e : v)
        if (fabs(e - x) < 0.00001)
            return true;
    return false;
}

class InharmonicsGenerator : public rack::Module
{
public:
    InharmonicsGenerator()
    {
        config(9,0,8);
        for (int i=0;i<9;++i)
        {
            configParam(i,-60.0,60.0,0.0);
        }
        for (int i=0;i<8;++i)
            last_outputs[i] = 0.0f;
        std::set<double> found;
        for (int i = 1; i < 32; ++i)
        {
            for (int j = 1; j < 32; ++j)
            {
                double r = (double)i / j;
                if (found.count(r) == 0)
                {
                    found.insert(r);
                    
                    //std::cout << "(" << i << "/" << j << ") ";
                }
                
            }
        }
        for (auto& e : found)
        {
            simple_intervals.push_back(e);
        }
        divider.setDivision(32);
    }
    void process(const ProcessArgs& args) override
    {
        if (divider.process())
        {
            float pitch = params[0].getValue();
            float center_freq = dsp::FREQ_C4*pow(2.0f,1.0f/12.0f*pitch);
            for (int i=0;i<8;++i)
            {
                if (outputs[i].isConnected())
                {
                    float offsetpitch = params[i+1].getValue();
                    float freqtocheck = dsp::FREQ_C4*pow(2.0f,1.0f/12.0f*offsetpitch);
                    if (fuzzyFind(simple_intervals, freqtocheck / center_freq) == false)
                    {
                        float outvolt = customlog(2.0f,freqtocheck/rack::dsp::FREQ_C4);
                        last_outputs[i] = outvolt;
                    }
                }
                outputs[i].setVoltage(last_outputs[i]);
            }
        }
    }
private:
    std::vector<double> simple_intervals;
    dsp::ClockDivider divider;
    float last_outputs[8];
};

class InharmonicsGeneratorWidget : public ModuleWidget
{
public:
    InharmonicsGeneratorWidget(InharmonicsGenerator* m)
    {
        box.size.x = 100;
        setModule(m);
        addParam(createParamCentered<RoundBlackKnob>(Vec(15, 60), m, 0));
        for (int i=0;i<8;++i)
        {
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(15, 90+i*30), m, i+1));
            addOutput(createOutputCentered<PJ301MPort>(Vec(45, 90+i*30), m, i));
        }
    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x40, 0x40, 0x40, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);

        nvgFontSize(args.vg, 15);
        nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        
        nvgText(args.vg, 3 , 10, "Inharmonics", NULL);
        nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }    
};

Model* modelInharmonics = createModel<InharmonicsGenerator, InharmonicsGeneratorWidget>("XInharmonics");
