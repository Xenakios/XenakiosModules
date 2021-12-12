#include "weightedrandom.h"
#include <random>

class WeightedRandomModule : public rack::Module
{
public:
    enum INPUTS
    {
        IN_GATE,
        ENUMS(IN_PROB, 8),
        IN_LAST
    };
    enum OUTPUTS
    {
        ENUMS(OUT_GATE, 8),
        OUT_CHOICE,
        OUT_LAST
    };
    enum paramids
	{
		ENUMS(PAR_W, 8),
        PAR_NUMSTEPS,
        LASTPAR
	};
    WeightedRandomModule();
    void process(const ProcessArgs& args) override;
private:
    dsp::SchmittTrigger m_trig;
    bool m_outcomes[8];
    float m_cur_discrete_output = 0.0f;
    bool m_in_trig_high = false;
    std::mt19937 m_rnd;
    std::uniform_real_distribution<float> m_unidist{0.0f,1.0f};
};

WeightedRandomModule::WeightedRandomModule()
{
    config(LASTPAR, IN_LAST, OUT_LAST, 0);
	for (int i=0;i<8;++i)
    {
        m_outcomes[i]=false;
        float defval = 0.0f;
        if (i == 0)
            defval = 50.0f;
        configParam(PAR_W+i, 0.0f, 100.0f, defval, "Weight "+std::to_string(i+1), "", 0, 1.0);
    }
    configParam(PAR_NUMSTEPS, 1.0f, 8.0f, 8.0f, "Num steps");
}

void WeightedRandomModule::process(const ProcessArgs& args)
{
    int numsteps = params[PAR_NUMSTEPS].getValue();
    float trigscaled = rescale(inputs[IN_GATE].getVoltage(), 0.1f, 2.f, 0.f, 1.f);
    if (m_trig.process(trigscaled))
    {
        // This maybe isn't the most efficient way to do this but since it's only run when
        // the clock triggers, maybe good enough for now...
        int result = 0;
        m_in_trig_high = true;
        float accum = 0.0f;
        float scaledvalues[8];
        for (int i=0;i<8;++i)
        {
            scaledvalues[i] = 0.0f;
            m_outcomes[i] = false;
        }
            
        for (int i=0;i<numsteps;++i)
        {
            float sv = params[PAR_W+i].getValue()+rescale(inputs[IN_PROB+i].getVoltage(),0.0,10.0f,0.0,100.0f);
            sv = clamp(sv,0.0,100.0f);
            accum += sv;
            scaledvalues[i] = sv;
        }
        if (accum>0.0f) // skip updates if all weights are zero. maybe need to handle this better?
        {
            float scaler = 1.0/accum;
            float z = m_unidist(m_rnd);
            accum = 0.0f;
            for (int i=0;i<numsteps;++i)
            {
                accum+=scaledvalues[i]*scaler;
                if (accum>=z)
                {
                    result = i;
                    break;
                }
            }
            if (result>=0 && result<numsteps)
            {
                for (int i=0;i<numsteps;++i)
                {
                    m_outcomes[i] = i == result;
                }
                if (numsteps>1)
                    m_cur_discrete_output = rescale((float)result,0.0f,numsteps-1,0.0f,10.0f);
                else m_cur_discrete_output = 0.0f;
            }
            
        }
        
    }
    for (int i=0;i<8;++i)
    {
        if (m_outcomes[i])
            outputs[OUT_GATE+i].setVoltage(inputs[IN_GATE].getVoltage());    
        else
            outputs[OUT_GATE+i].setVoltage(0.0f);    
    }
    outputs[OUT_CHOICE].setVoltage(m_cur_discrete_output);
}

class WeightedRandomWidget : public ModuleWidget
{
public:
    WeightedRandomWidget(WeightedRandomModule* mod);
    void draw(const DrawArgs &args) override;
private:

};


WeightedRandomWidget::WeightedRandomWidget(WeightedRandomModule* mod)
{
    setModule(mod);
    box.size.x = 9*RACK_GRID_WIDTH;
    
    addInput(createInput<PJ301MPort>(Vec(5, 20), module, WeightedRandomModule::IN_GATE));
    addOutput(createOutput<PJ301MPort>(Vec(85, 20), module, WeightedRandomModule::OUT_CHOICE));
    for (int i=0;i<8;++i)
    {
        addInput(createInput<PJ301MPort>(Vec(5, 50+i*40), module, WeightedRandomModule::IN_PROB+i));
        addParam(createParam<RoundLargeBlackKnob>(Vec(38, 40+i*40), module, WeightedRandomModule::PAR_W+i));
        addOutput(createOutput<PJ301MPort>(Vec(85, 50+i*40), module, WeightedRandomModule::OUT_GATE+i));
        
    }
    Trimpot* pot = nullptr;
    addParam(pot = createParam<Trimpot>(Vec(38, 20), module, WeightedRandomModule::PAR_NUMSTEPS));
    pot->snap = true;
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
    nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3 , 10, "Octauilli Gate", NULL);
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
	ModuleWidget::draw(args);
}

Model* modelWeightGate = createModel<WeightedRandomModule,WeightedRandomWidget>("WeightedRandom");

class MyParam : public ParamQuantity
{
public:
    MyParam() : ParamQuantity() {}
    float getDisplayValue() override
    {
        return getValue();
    }
};

class ReducerModule : public rack::Module
{
public:
    enum PARS
    {
        PAR_ALGO,
        PAR_A,
        PAR_B,
        PAR_LAST
    };
    enum ALGOS
    {
        ALGO_ADD,
        ALGO_AVG,
        ALGO_MULT,
        ALGO_MIN,
        ALGO_MAX,
        ALGO_AND,
        ALGO_OR,
        ALGO_XOR,
        ALGO_DIFFERENCE,
        ALGO_ROUNDROBIN,
        ALGO_LAST
    };
    ReducerModule();
    void process(const ProcessArgs& args) override;
    const char* getAlgoName()
    {
        int algo = params[PAR_ALGO].getValue();
        static const char* algonames[]={"Add","Avg","Mult","Min","Max","And","Or","Xor","Diff","RR"};
        return algonames[algo];
    }
private:
    RoundRobin m_rr;  
};

class ReducerWidget : public ModuleWidget
{
public:
    ReducerWidget(ReducerModule*);
    void draw(const DrawArgs &args) override;
private:
    ReducerModule* m_mod = nullptr;
};

ReducerModule::ReducerModule()
{
    config(3,8,1);
    configParam(PAR_ALGO,0.0f,ALGO_LAST-1,0.0f);
    configParam(PAR_A,0.0f,1.0f,0.0f);
    configParam<MyParam>(PAR_B,0.0f,1.0f,0.0f);
}

void ReducerModule::process(const ProcessArgs& args)
{
    int algo = params[PAR_ALGO].getValue();
    float p_a = params[PAR_A].getValue();
    //float p_b = params[PAR_B].getValue();
    float r = 0.0f;
    if (algo == ALGO_ADD)
        r = reduce_add(inputs,0.0f,0.0f);
    else if (algo == ALGO_AVG)
        r = reduce_average(inputs,0.0f,0.0f);
    else if (algo == ALGO_MULT)
        r = reduce_mult(inputs,p_a,0.0f);
    else if (algo == ALGO_MIN)
        r = reduce_min(inputs,0.0f,1.0f);
    else if (algo == ALGO_MAX)
        r = reduce_max(inputs,0.0f,1.0f);
    else if (algo == ALGO_ROUNDROBIN)
        r = m_rr.process(inputs);
    else if (algo == ALGO_AND)
        r = reduce_and(inputs,0.0f,0.0f);
    else if (algo == ALGO_OR)
        r = reduce_or(inputs,0.0f,0.0f);
    else if (algo == ALGO_XOR)
        r = reduce_xor(inputs,0.0f,0.0f);
    else if (algo == ALGO_DIFFERENCE)
        r = reduce_difference(inputs,p_a,0.0f);
    outputs[0].setVoltage(clamp(r,-10.0f,10.0f));
}

ReducerWidget::ReducerWidget(ReducerModule* m)
{
    setModule(m);
    box.size.x = 120;
    m_mod = m;
    for (int i=0;i<8;++i)
    {
        addInput(createInput<PJ301MPort>(Vec(5,30+30*i), module, i));
    }
    addOutput(createOutput<PJ301MPort>(Vec(5,30+8*30), module, 0));
    addParam(createParam<RoundBlackKnob>(Vec(5, 30+30*9), module, ReducerModule::PAR_ALGO));    
    addParam(createParam<RoundBlackKnob>(Vec(40, 30+30*9), module, ReducerModule::PAR_A));    
    addParam(createParam<RoundBlackKnob>(Vec(75, 30+30*9), module, ReducerModule::PAR_B));    
}

void ReducerWidget::draw(const DrawArgs &args)
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
    nvgText(args.vg, 3 , 10, "Reducer", NULL);
    char buf[100];
    if (m_mod)
        sprintf(buf,"Xenakios %s",m_mod->getAlgoName());
    else sprintf(buf,"Xenakios");
    nvgText(args.vg, 3 , h-11, buf, NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}

Model* modelReducer = createModel<ReducerModule,ReducerWidget>("Reduce");

class HistogramModule : public rack::Module
{
public:
    HistogramModule();
    void process(const ProcessArgs& args) override;
    std::vector<int>* getData()
    {
        return &m_data;
    }
private:
    std::vector<int> m_data;
    int m_data_size = 128;
    float m_volt_min = -10.0f;
    float m_volt_max = 10.0f;
    dsp::SchmittTrigger m_reset_trig;
};

class HistogramWidget : public TransparentWidget
{
public:
    HistogramWidget(HistogramModule* m) { m_mod = m; }
    void draw(const DrawArgs &args) override;
    
    
private:
    HistogramModule* m_mod = nullptr;
};

class HistogramModuleWidget : public ModuleWidget
{
public:
    HistogramModuleWidget(HistogramModule* mod);
    void draw(const DrawArgs &args) override;
    
private:
    HistogramWidget* m_hwid = nullptr;
};

HistogramModule::HistogramModule()
{
    m_data.resize(m_data_size);
    config(1,2,0,0);
    configParam(0,0.0f,1.0f,0.0f);
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
        if (m_mod==nullptr)
            return;
        nvgSave(args.vg);
		float w = box.size.x;
		float h = box.size.y;
		nvgBeginPath(args.vg);
		nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
		nvgRect(args.vg,0.0f,0.0f,w,h);
		nvgFill(args.vg);
        
        auto data = m_mod->getData();
        auto maxe = *std::max_element(data->begin(),data->end());
        float yscaler = h/maxe;
        float manualscale = m_mod->params[0].getValue();
        if (manualscale>0.0f)
            yscaler = h/10000.0f*manualscale;
        float barwidth = w/data->size();
        for (int i=0;i<(int)data->size();++i)
        {
            float xcor = rescale(i,0,data->size()-1,0,w-barwidth);
            float y = (*data)[i]*yscaler;
            y = clamp(y,0.0f,h);
            if (y>=1.0f)
            {
                nvgBeginPath(args.vg);
                nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
                nvgRect(args.vg,xcor,h-y,barwidth,y);
                nvgFill(args.vg);
            }
            
            
        }
        nvgRestore(args.vg);
    }

HistogramModuleWidget::HistogramModuleWidget(HistogramModule* mod_)
{
    box.size.x = 500;
    setModule(mod_);
    addInput(createInput<PJ301MPort>(Vec(5, 20), module, 0));
    addInput(createInput<PJ301MPort>(Vec(35, 20), module, 1));
    addParam(createParam<RoundBlackKnob>(Vec(65, 17), module, 0));    
    m_hwid = new HistogramWidget(mod_);
    
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
    nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3 , 10, "Histogram", NULL);
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}

Model* modelHistogram = createModel<HistogramModule,HistogramModuleWidget>("Histogram");
