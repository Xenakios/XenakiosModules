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

MatrixSwitchModule::MatrixSwitchModule()
{
    m_curoutputs.resize(32);
    m_cd.setDivision(256);
    config(0,18,16,0);
    m_connections[0].reserve(128);
    m_connections[1].reserve(128);
    /*
    m_connections.emplace_back(0,0);
    m_connections.emplace_back(0,5);
    m_connections.emplace_back(15,1);
    m_connections.emplace_back(15,14);
    m_connections.emplace_back(8,7);
    */
}

void MatrixSwitchModule::process(const ProcessArgs& args)
{
    for (int i=0;i<(int)outputs.size();++i)
    {
        //outputs[i].setVoltage(0.0f);
        m_curoutputs[i]=0.0f;
    }
    // this is very nasty, need to figure out some other way to deal with the thread safety
    if (m_state == 1)
    {
        //return;
    }
    auto renderfunc = [this](const std::vector<connection>& cons, int xfade=0)
    {
        /*
        float xfadegain = 1.0f;
        if (xfade == 1)
            xfadegain = 1.0f-1.0f/m_crossfadelen*m_crossfadecounter;
        else if (xfade == 2)
            xfadegain = 1.0f/m_crossfadelen*m_crossfadecounter;
        */
        for (int i=0;i<(int)cons.size();++i)
        {
            int src = cons[i].m_src;
            int dest = cons[i].m_dest;
            float v = m_curoutputs[dest];
            v += inputs[src].getVoltage();
            m_curoutputs[dest]=v;
        }
    };
    if (m_state < 2)
    {
        renderfunc(m_connections[0],0);
    }
    if (m_state == 2)
    {
        
        ++m_crossfadecounter;
        if (m_crossfadecounter==m_crossfadelen)
        {
            m_crossfadecounter = 0;
            m_state = 0;
        }
    }
}

json_t* MatrixSwitchModule::dataToJson()
{
    json_t* resultJ = json_object();
    json_t* arrayJ = json_array();
    for (int i=0;i<(int)m_connections[m_activeconnections].size();++i)
    {
        json_t* conJ = json_object();
        json_object_set(conJ,"src",json_integer(m_connections[m_activeconnections][i].m_src));
        json_object_set(conJ,"dest",json_integer(m_connections[m_activeconnections][i].m_dest));
        json_array_append(arrayJ,conJ);
    }
    json_object_set(resultJ,"connections",arrayJ);
    return resultJ;
}

void MatrixSwitchModule::dataFromJson(json_t* root)
{
    json_t* arrayJ = json_object_get(root,"connections");
    if (arrayJ==nullptr)
        return;
    int numcons = json_array_size(arrayJ);
    if (numcons>0 && numcons<256)
    {
        m_state = 1;
        m_connections[m_activeconnections].clear();
        for (int i=0;i<numcons;++i)
        {
            json_t* conJ = json_array_get(arrayJ,i);
            int src = json_integer_value(json_object_get(conJ,"src"));
            int dest = json_integer_value(json_object_get(conJ,"dest"));
            m_connections[m_activeconnections].emplace_back(src,dest);
        }
        m_state = 0;
    }
}

bool MatrixSwitchModule::isConnected(int x, int y)
{
    if (x<0 || x>18)
        return false;
    if (y<0 || y>16)
        return false;
    for (int i=0;i<(int)m_connections[m_activeconnections].size();++i)
    {
        if (m_connections[m_activeconnections][i].m_src == x && m_connections[m_activeconnections][i].m_dest == y)
        {
            return true;
        }
    }
    return false;
}

void MatrixSwitchModule::setConnected(int x, int y, bool c)
{
    if (x<0 || x>18)
        return;
    if (y<0 || y>16)
        return;
    if (c == true && isConnected(x,y))
        return;
    if (c == true)
    {
        m_state = 1;
        m_connections[1]=m_connections[0];
        m_connections[1].emplace_back(x,y);
        m_state = 2; 
    }
    if (c == false)
    {
        for (int i=0;i<(int)m_connections[m_activeconnections].size();++i)
        {
            if (m_connections[m_activeconnections][i].m_src == x && m_connections[m_activeconnections][i].m_dest == y)
            {
                m_state = 1;
                m_connections[1]=m_connections[0];
                m_connections[1].erase(m_connections[1].begin()+i);
                m_state = 2;
                return;
            }
        }
    }
}

MatrixSwitchWidget::MatrixSwitchWidget(MatrixSwitchModule* module_)
{
    setModule(module_);
    box.size.x = 500;
    for (int i=0;i<18;++i)
    {
        int x = i / 9;
        int y = i % 9;
        float ycor = 20.0f+y*25.0;
        addInput(createInput<PJ301MPort>(Vec(5+25.0f*x, ycor), module, i));
    }
    for (int i=0;i<16;++i)
    {
        int x = i / 8;
        int y = i % 8;
        float ycor = 20.0f+y*25.0;
        addOutput(createOutput<PJ301MPort>(Vec(450.0+25.0f*x, ycor), module, i));
    }
    MatrixGridWidget* grid = new MatrixGridWidget(module_);
    grid->box.pos.x = 60;
    grid->box.pos.y = 20;
    grid->box.size.x = 330;
    addChild(grid);
}

void MatrixSwitchWidget::draw(const DrawArgs &args)
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
    nvgText(args.vg, 3 , 10, "Matrix switch", NULL);
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}

MatrixGridWidget::MatrixGridWidget(MatrixSwitchModule* mod_)
{
    m_mod = mod_;
}

void MatrixGridWidget::onButton(const event::Button& e)
{
    float w = box.size.x;
    float boxsize = w/16;
    if (e.action==GLFW_PRESS)
    {
        int x = (e.pos.x/boxsize);
        int y = (e.pos.y/boxsize);
        bool isconnected = m_mod->isConnected(x,y);
        m_mod->setConnected(x,y,!isconnected);
    }
    
}

void MatrixGridWidget::draw(const DrawArgs &args)
{
    if (m_mod==nullptr)
        return;
    nvgSave(args.vg);
    float w = box.size.x;
    //float h = box.size.y;
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    float boxsize = w/16.0-1.0;
    for (int i=0;i<18;++i)
    {
        for (int j=0;j<16;++j)
        {
            float xcor = w/16*i;
            float ycor = w/16*j;
            nvgBeginPath(args.vg);
            nvgRect(args.vg,xcor,ycor,boxsize,boxsize);
            nvgFill(args.vg);
        }
    }
    nvgFillColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0xff));
    auto& cons = m_mod->getConnections();
    for (auto& con : cons)
    {
        float xcor = w/16*con.m_src;
        float ycor = w/16*con.m_dest;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg,xcor,ycor,boxsize,boxsize,8.0f);
        nvgFill(args.vg);
    }
    nvgRestore(args.vg);
}

class MyParam : public ParamQuantity
{
public:
    MyParam() : ParamQuantity() {}
    float getDisplayValue() override
    {
        return getValue();
    }
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


