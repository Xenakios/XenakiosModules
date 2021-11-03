#include "xenutils.h"

extern std::shared_ptr<Font> g_font;

DecahexCVTransformerWidget::DecahexCVTransformerWidget(DecahexCVTransformer* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
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
    nvgFontFaceId(args.vg, g_font->handle);
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
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
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
    nvgFontFaceId(args.vg, g_font->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3 , 10, "Derivator", NULL);
    
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}