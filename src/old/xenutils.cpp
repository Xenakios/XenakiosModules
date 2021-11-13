#include "xenutils.h"

class DerivatorModule : public rack::Module
{
public:
    DerivatorModule()
    {
        config(2,1,2);
        configParam(0,0.0,1.0,0.5,"Scaler");
        configParam(1,0.00001,1.0,1.0,"Delta");
    }
    void process(const ProcessArgs& args) override
    {
        float involt = inputs[0].getVoltage();
        float delta = params[1].getValue();
        float interp = m_history[1]+(involt-m_history[1])*(1.0-delta);
        //float deriv1 = involt-m_history[1];
        float deriv1 = (involt-interp)/(delta);
        float deriv2 = involt-2.0f*m_history[1]+m_history[0];
        float scaled = deriv1*(std::pow(2.0,rescale(params[0].getValue(),0.0f,1.0f,0.0,12.0))-1.0f);
        scaled = clamp(scaled,-10.0f,10.0f);
        outputs[0].setVoltage(scaled);
        outputs[1].setVoltage(deriv2);
        m_history[0] = m_history[1];
        m_history[1] = involt;
    }
private:
    float m_history[2] = {0.0f,0.0f};
    
};

class DerivatorWidget : public ModuleWidget
{
public:
    DerivatorWidget(DerivatorModule*);
    void draw(const DrawArgs &args) override;  
};

Model* modelXDerivator = createModel<DerivatorModule,DerivatorWidget>("Derivator");

DecahexCVTransformerWidget::DecahexCVTransformerWidget(DecahexCVTransformer* m)
{
    setModule(m);
    box.size.x = 550;
    
    for (int i=0;i<8;++i)
    {
        addInput(createInput<PJ301MPort>(Vec(5,30+30*i), module, DecahexCVTransformer::INSIG+i));
        addParam(createParam<RoundSmallBlackKnob>(Vec(35, 30+30*i), module, DecahexCVTransformer::INLOW+i)); 
        addParam(createParam<RoundSmallBlackKnob>(Vec(65, 30+30*i), module, DecahexCVTransformer::INHIGH+i)); 
        addParam(createParam<RoundSmallBlackKnob>(Vec(95, 30+30*i), module, DecahexCVTransformer::TRANSFORMTYPE+i)); 
        addParam(createParam<RoundSmallBlackKnob>(Vec(125, 30+30*i), module, DecahexCVTransformer::TRANSFORMPARA+i)); 
        addParam(createParam<RoundSmallBlackKnob>(Vec(155, 30+30*i), module, DecahexCVTransformer::TRANSFORMPARB+i)); 
        addParam(createParam<RoundSmallBlackKnob>(Vec(185, 30+30*i), module, DecahexCVTransformer::OUTOFFSET+i)); 
        addParam(createParam<RoundSmallBlackKnob>(Vec(215, 30+30*i), module, DecahexCVTransformer::OUTGAIN+i)); 
        addOutput(createOutput<PJ301MPort>(Vec(245,30+30*i), module, DecahexCVTransformer::OUTSIG+i));
    }
    
    
}

void DecahexCVTransformerWidget::draw(const DrawArgs &args)
{
    nvgSave(args.vg);
    float w = box.size.x;
    float h = box.size.y;
    nvgBeginPath(args.vg);
    nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
    nvgRect(args.vg,0.0f,0.0f,w,h);
    nvgFill(args.vg);

    nvgFontSize(args.vg, 15);
    nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3 , 10, "DecahexCVTransformer", NULL);
    
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    
    nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
    
    nvgBeginPath(args.vg);
    nvgRoundedRect(args.vg,244,26,29,240,5.0f);
    nvgFill(args.vg);
    
    

    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}

DerivatorWidget::DerivatorWidget(DerivatorModule* m)
{
    setModule(m);
    box.size.x = 60;
    addInput(createInput<PJ301MPort>(Vec(5,30), m, 0));
    addOutput(createOutput<PJ301MPort>(Vec(5,60), m, 0));
    addOutput(createOutput<PJ301MPort>(Vec(5,90), m, 1));
    addParam(createParam<RoundSmallBlackKnob>(Vec(5, 120), module, 0)); 
    addParam(createParam<RoundSmallBlackKnob>(Vec(5, 150), module, 1)); 
}

void DerivatorWidget::draw(const DrawArgs &args)
{
    nvgSave(args.vg);
    float w = box.size.x;
    float h = box.size.y;
    nvgBeginPath(args.vg);
    nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
    nvgRect(args.vg,0.0f,0.0f,w,h);
    nvgFill(args.vg);

    nvgFontSize(args.vg, 15);
    nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3 , 10, "Derivator", NULL);
    
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}