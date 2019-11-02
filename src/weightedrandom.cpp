#include "weightedrandom.h"

WeightedRandomModule::WeightedRandomModule()
{
    config(LASTPAR, 9, 8, 0);
	for (int i=0;i<WR_NUM_OUTPUTS;++i)
    {
        m_outcomes[i]=false;
        float defval = 0.0f;
        if (i == 0)
            defval = 50.0f;
        configParam(W_0+i, 0.0f, 100.0f, defval, "Weight "+std::to_string(i+1), "", 0, 1.0);
    }
}

void WeightedRandomModule::process(const ProcessArgs& args)
{
    float trigscaled = rescale(inputs[0].getVoltage(), 0.1f, 2.f, 0.f, 1.f);
    if (m_trig.process(trigscaled))
    {
        // This maybe isn't the most efficient way to do this but since it's only run when
        // the clock triggers, maybe good enough for now...
        int result = 0;
        m_in_trig_high = true;
        float accum = 0.0f;
        float scaledvalues[8];
        for (int i=0;i<WR_NUM_OUTPUTS;++i)
        {
            float sv = params[W_0+i].getValue()+rescale(inputs[i+1].getVoltage(),0.0,10.0f,0.0,100.0f);
            sv = clamp(sv,0.0,100.0f);
            accum+=sv;
            scaledvalues[i]=sv;
        }
        if (accum>0.0f) // skip updates if all weights are zero. maybe need to handle this better?
        {
            float scaler = 1.0/accum;
            float z = rack::random::uniform();
            accum = 0.0f;
            for (int i=0;i<WR_NUM_OUTPUTS;++i)
            {
                accum+=scaledvalues[i]*scaler;
                if (accum>=z)
                {
                    result = i;
                    break;
                }
            }
            if (result>=0 && result<WR_NUM_OUTPUTS)
            {
                for (int i=0;i<WR_NUM_OUTPUTS;++i)
                {
                    m_outcomes[i] = i == result;
                }
                
            }
            
        }
        
    }
    for (int i=0;i<WR_NUM_OUTPUTS;++i)
    {
        if (m_outcomes[i])
            outputs[i].setVoltage(inputs[0].getVoltage());    
        else
            outputs[i].setVoltage(0.0f);    
    }
}

extern std::shared_ptr<Font> g_font;

WeightedRandomWidget::WeightedRandomWidget(WeightedRandomModule* mod)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(mod);
    box.size.x = 130;
    
    addInput(createInput<PJ301MPort>(Vec(5, 20), module, 0));
    
    for (int i=0;i<WR_NUM_OUTPUTS;++i)
    {
        addInput(createInput<PJ301MPort>(Vec(5, 50+i*40), module, i+1));
        addParam(createParam<RoundLargeBlackKnob>(Vec(38, 40+i*40), module, i));
        addOutput(createOutput<PJ301MPort>(Vec(85, 50+i*40), module, i));
        
    }
}

void WeightedRandomWidget::draw(const DrawArgs &args)
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
    nvgText(args.vg, 3 , 10, "Octauilli Gate", NULL);
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
	ModuleWidget::draw(args);
}

HistogramModule::HistogramModule()
{
    m_data.resize(m_data_size);
    config(0,2,0,0);
}

void HistogramModule::process(const ProcessArgs& args) 
{
    if (m_reset_trig.process(inputs[1].getVoltage()))
    {
        std::fill(m_data.begin(),m_data.end(),0);
    }
    if (inputs[0].isConnected())
    {
        float v = inputs[0].getVoltage();
        if (v>=m_volt_min && v<=m_volt_max)
        {
            int index = rescale(v,m_volt_min,m_volt_max,0,m_data.size()-1);
            ++m_data[index];
        }
    }
}

void HistogramWidget::draw(const DrawArgs &args) 
    {
        nvgSave(args.vg);
		float w = box.size.x;
		float h = box.size.y;
		nvgBeginPath(args.vg);
		nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
		nvgRect(args.vg,0.0f,0.0f,w,h);
		nvgFill(args.vg);
        if (DataRequestFunc.operator bool())
        {
            auto data = DataRequestFunc();
            auto maxe = *std::max_element(data.begin(),data.end());
            float yscaler = h/maxe;
            for (int i=0;i<data.size();++i)
            {
                float xcor = rescale(i,0,data.size()-1,0,w);
                float y = data[i]*yscaler;
                //continue;
                if (y>=1.0f)
                {
                    nvgBeginPath(args.vg);
			        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
                    nvgRect(args.vg,xcor,h-y,2.0f,y);
			        nvgFill(args.vg);
                }
                
                
            }
            
        }
        nvgRestore(args.vg);
    }

HistogramModuleWidget::HistogramModuleWidget(HistogramModule* mod_)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    box.size.x = 500;
    setModule(mod_);
    addInput(createInput<PJ301MPort>(Vec(5, 20), module, 0));
    addInput(createInput<PJ301MPort>(Vec(35, 20), module, 1));
    m_hwid = new HistogramWidget;
    if (mod_!=nullptr)
        m_hwid->DataRequestFunc = [mod_](){ return mod_->getData(); };
    m_hwid->box.pos = Vec(5,50);
    m_hwid->box.size = Vec(box.size.x-10,300);
    addChild(m_hwid);
}

void HistogramModuleWidget::draw(const DrawArgs &args)
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
    nvgText(args.vg, 3 , 10, "Histogram", NULL);
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}