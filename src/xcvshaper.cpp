#include "plugin.hpp"
#include "helperwidgets.h"

class ShaperEngine
{
public:
    ShaperEngine()
    {
        quantizeGrid.reserve(4096);
    }
    float process(float input, float amount)
    {
        return quantize_to_grid(input,quantizeGrid, amount);
    }
    void updateGrid(float stepsize, float offs)
    {
        if (stepsize==curStepSize && offs == curOffset)
            return;
        quantizeGrid.clear();
        float y = -5.0f;
        int sanity = 0;
        while (y<=5.0f)
        {
            quantizeGrid.push_back(y+offs);
            y+=stepsize;
            ++sanity;
            if (sanity>=quantizeGrid.capacity())
                break;
        }
        curStepSize = stepsize;
        curOffset = offs;
    }
    void updateOffset(float offs)
    {
        if (offs == curOffset)
            return;
        for(int i=0;i<quantizeGrid.size();++i)
        {
            float y = quantizeGrid[i];
            //y = clamp(y+offs,-5.0f,5.0f);
            quantizeGrid[i] = y+offs;
        }
        curOffset = offs;
    }
    float curStepSize = 0.0f;
    float curOffset = 0.0f;
    std::vector<float> quantizeGrid;
};

class CVShaper : public Module
{
public:
    enum PARAMS
    {
        PAR_QUAN_AMT,
        PAR_QUAN_OFFS,
        PAR_QUAN_STEPSIZE,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_CV,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_CV,
        OUT_LAST
    };
    CVShaper()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_QUAN_AMT,0.0f,1.0f,1.0f,"Quantization amount");
        configParam(PAR_QUAN_OFFS,-1.0f,1.0f,0.0f,"Quantization offset");
        configParam(PAR_QUAN_STEPSIZE,0.01f,5.0f,1.0f,"Quantization step size");
    }
    void process(const ProcessArgs& args) override
    {
        float amt = params[PAR_QUAN_AMT].getValue();
        float offs = params[PAR_QUAN_OFFS].getValue();
        float ssize = params[PAR_QUAN_STEPSIZE].getValue();
        m_engines[0].updateGrid(ssize,offs);
        
        float input = inputs[IN_CV].getVoltage();
        float output = m_engines[0].process(input,amt);
        outputs[OUT_CV].setVoltage(output);
    }
    ShaperEngine m_engines[16];
};

class CVShaperWidget : public ModuleWidget
{
public:
    CVShaperWidget(CVShaper* mod)
    {
        setModule(mod);
        box.size.x = 20*15;
        RoundSmallBlackKnob* pot;
        addParam(pot = createParam<RoundSmallBlackKnob>(Vec(1, 50), module, CVShaper::PAR_QUAN_AMT));
        addParam(pot = createParam<RoundSmallBlackKnob>(Vec(31, 50), module, CVShaper::PAR_QUAN_OFFS));
        addParam(pot = createParam<RoundSmallBlackKnob>(Vec(61, 50), module, CVShaper::PAR_QUAN_STEPSIZE));
        PortWithBackGround<PJ301MPort>* port = nullptr;
        addInput(port = createInput<PortWithBackGround<PJ301MPort>>(Vec(91, 50), mod, CVShaper::IN_CV));
        port->m_text = "INPUT";
        port->m_is_out = false;
        addOutput(port = createOutput<PortWithBackGround<PJ301MPort>>(Vec(121, 50), mod, CVShaper::OUT_CV));
        port->m_text = "OUT";
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "CV SHAPER",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
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
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelXCVShaper = createModel<CVShaper, CVShaperWidget>("XCVShaper");
