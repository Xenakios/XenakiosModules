#include "clocks.h"

extern std::shared_ptr<Font> g_font;

class RandomClockModule : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_MASTER_DENSITY,
        ENUMS(PAR_DENSITY_MULTIP, 8),
        ENUMS(PAR_GATE_LEN, 8),
        PAR_LAST
    };
    RandomClockModule();
    void process(const ProcessArgs& args) override;
    float m_curDensity = 0.0f;
private:
    RandomClock m_clocks[8];
};

RandomClockModule::RandomClockModule()
{
    config(PAR_LAST,1,16);
    configParam(0,0.0f,1.0f,0.1,"Master density"); // master clock density
    float defmult = rescale(1.0f,0.1f,10.0f,0.0f,1.0f);
    for (int i=0;i<8;++i)
    {
        configParam(PAR_DENSITY_MULTIP+i,0.0f,1.0f,defmult,"Density multiplier "+std::to_string(i+1)); // clock multiplier
        // gate len
        // >=0.0 && <=0.5 deterministic percentage 1% to 99% of clock interval
        // >0.5 && <=1.0 stochastic distribution favoring short and long values
        configParam(PAR_GATE_LEN+i,0.0,1.0f,0.25f,"Gate length "+std::to_string(i+1)); 
    }
}



class RandomClockWidget : public ModuleWidget
{
public:
    RandomClockWidget(RandomClockModule*);
    void draw(const DrawArgs &args) override;
private:
    RandomClockModule* m_mod = nullptr;
};

void RandomClockModule::process(const ProcessArgs& args)
{
    float masterdensity = params[PAR_MASTER_DENSITY].getValue();
    if (masterdensity<0.5f)
    {
        masterdensity = rescale(masterdensity,0.0f,0.5f,0.0f,1.0f);
        masterdensity = pow(masterdensity,0.7f);
        masterdensity = rescale(masterdensity,0.0f,1.0f,0.05f,1.0f);
    }
    else
    {
        masterdensity = rescale(masterdensity,0.5f,1.0f,0.0f,1.0f);
        masterdensity = pow(masterdensity,4.0f);
        masterdensity = rescale(masterdensity,0.0f,1.0f,1.0f,200.0f);
    }
    m_curDensity = masterdensity;
    
    for (int i=0;i<8;++i)
    {
        if (outputs[i].isConnected())
        {
            float multip = rescale(params[PAR_DENSITY_MULTIP+i].getValue(),0.0f,1.0f,0.1f,10.0f);
            m_clocks[i].setDensity(masterdensity*multip);
            float glen = params[PAR_GATE_LEN+i].getValue();
            m_clocks[i].setGateLen(glen);
            outputs[i].setVoltage(10.0f*m_clocks[i].process(args.sampleTime));    
        }
        if (outputs[i+8].isConnected())
        {
            outputs[i+8].setVoltage(m_clocks[i].getCurrentInterval());
        }
    }
    
}

RandomClockWidget::RandomClockWidget(RandomClockModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = 130;
    m_mod = m;
    for (int i=0;i<8;++i)
    {
        addOutput(createOutput<PJ301MPort>(Vec(5,30+30*i), module, i));
        addParam(createParam<RoundSmallBlackKnob>(Vec(35, 30+30*i), module, RandomClockModule::PAR_DENSITY_MULTIP+i)); 
        addParam(createParam<RoundSmallBlackKnob>(Vec(65, 30+30*i), module, RandomClockModule::PAR_GATE_LEN+i)); 
        addOutput(createOutput<PJ301MPort>(Vec(95,30+30*i), module, i+8));
    }
    addParam(createParam<RoundBlackKnob>(Vec(5, 30+30*8), module, RandomClockModule::PAR_MASTER_DENSITY));    
    
}

void RandomClockWidget::draw(const DrawArgs &args)
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
    nvgText(args.vg, 3 , 10, "Random clock", NULL);
    char buf[100];
    if (m_mod)
        sprintf(buf,"Xenakios %f",m_mod->m_curDensity);
    else
    {
        sprintf(buf,"Xenakios");
    }
    
    nvgText(args.vg, 3 , h-11, buf, NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}

class DivisionClockModule : public rack::Module
{
public:
    
    DivisionClockModule()
    {
        m_cd.setDivision(128);
        config(33,25,16);
        for (int i=0;i<8;++i)
            configParam(i,1.0f,32.0f,4.0f,"Main div");
        for (int i=0;i<8;++i)
            configParam(8+i,1.0f,32.0f,1.0f,"Sub div");
        for (int i=0;i<8;++i)
            configParam(16+i,0.01f,0.99f,0.5f,"Gate len");
        for (int i=0;i<8;++i)
            configParam(24+i,0.0f,1.0f,0.0f,"Phase offset");
        configParam(32,30.0f,240.0f,60.0f,"BPM");
    }
    void process(const ProcessArgs& args) override;
    
private:
    DividerClock m_clocks[8];
    dsp::SchmittTrigger m_reset_trig;
    dsp::ClockDivider m_cd;
};

class DividerClockWidget : public ModuleWidget
{
public:
    DividerClockWidget(DivisionClockModule* m);
    void draw(const DrawArgs &args) override;
    
};

DividerClockWidget::DividerClockWidget(DivisionClockModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = 290;
    for (int i=0;i<8;++i)
    {
        addParam(createParam<RoundSmallBlackKnob>(Vec(5, 30+30*i), module, i)); 
        addInput(createInput<PJ301MPort>(Vec(35,30+30*i), module, i+1));
        addParam(createParam<RoundSmallBlackKnob>(Vec(65, 30+30*i), module, i+8)); 
        addInput(createInput<PJ301MPort>(Vec(95,30+30*i), module, i+9));
        addParam(createParam<RoundSmallBlackKnob>(Vec(125, 30+30*i), module, i+16)); 
        addInput(createInput<PJ301MPort>(Vec(155,30+30*i), module, i+17));
        addParam(createParam<RoundSmallBlackKnob>(Vec(185, 30+30*i), module, i+24)); 
        addOutput(createOutput<PJ301MPort>(Vec(225,30+30*i), module, i));
        addOutput(createOutput<PJ301MPort>(Vec(255,30+30*i), module, i+8));
    }
    addInput(createInput<PJ301MPort>(Vec(5,30+30*8), module, 0));
    addParam(createParam<RoundLargeBlackKnob>(Vec(65, 30+30*8), module, 32)); 
}

void DividerClockWidget::draw(const DrawArgs &args)
{
    nvgSave(args.vg);
    float w = box.size.x;
    float h = box.size.y;
    nvgBeginPath(args.vg);
    nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
    nvgRect (args.vg,0.0f,0.0f,w,h);
    nvgFill(args.vg);

    nvgFontSize(args.vg, 15);
    nvgFontFaceId(args.vg, g_font->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3 , 10, "Divider clock", NULL);
    
    
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}

void DivisionClockModule::process(const ProcessArgs& args) 
{
    
    if (m_reset_trig.process(inputs[0].getVoltage()))
    {
        for (int i=0;i<8;++i)
        {
            m_clocks[i].reset();
        }
    }
    const float bpm = params[32].getValue();
    bool updateparams = m_cd.process();
    static const int divvalues_a[5]={1,2,4,8,16};
    static const int divvalues_b[8]={1,2,3,4,6,8,12,16};
    static const int divvalues_c[14]={1,2,3,4,5,6,7,8,9,11,12,13,15,16};
    int dtabsize = 14;
    for (int i=0;i<8;++i)
    {
        //if (outputs[i].isConnected() || outputs[i+8].isConnected())
        {
            if (updateparams)
            {
                float v = params[i].getValue()+rescale(inputs[i+1].getVoltage(),0.0,10.0f,0.0,31.0f);
                v = clamp(v,1.0,32.0);
                float len = 60.0f/bpm/4.0f*v;
                float subdivs = params[i+8].getValue();
                //subdivs += rescale(inputs[i+9].getVoltage(),0.0,10.0f,0.0,31.0f);
                float sdiv_cv = rescale(inputs[i+9].getVoltage(),0.0,10.0f,0.0,(float)dtabsize-1);
                sdiv_cv = clamp(sdiv_cv,0.0f,(float)dtabsize-1);
                int sdiv_index = sdiv_cv;
                subdivs *= divvalues_c[sdiv_index];
                subdivs = clamp(subdivs,1.0,32.0);
                
                float offs = params[i+24].getValue();
                m_clocks[i].setParams(len,subdivs,offs,false);
                v = params[i+16].getValue()+rescale(inputs[i+17].getVoltage(),0.0,10.0f,0.0,0.99f);
                m_clocks[i].setGateLen(v); // clamped in the clock method
            }
            outputs[i].setVoltage(m_clocks[i].process(args.sampleTime)*10.0f);
            outputs[i+8].setVoltage(m_clocks[i].mainClockHigh()*10.0f);
        }
    }
    
}

Model* modelPolyClock = createModel<DivisionClockModule, DividerClockWidget>("DividerClock");
Model* modelRandomClock = createModel<RandomClockModule,RandomClockWidget>("RandomClock");
