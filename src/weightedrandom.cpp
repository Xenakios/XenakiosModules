#include "weightedrandom.h"

WeightedRandomModule::WeightedRandomModule()
{
    config(LASTPAR, 9, 9, 0);
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
                m_cur_discrete_output = rescale(result,0,7,0.0f,10.0f);
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
    outputs[WR_NUM_OUTPUTS].setVoltage(m_cur_discrete_output);
}

extern std::shared_ptr<Font> g_font;

WeightedRandomWidget::WeightedRandomWidget(WeightedRandomModule* mod)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(mod);
    box.size.x = 130;
    
    addInput(createInput<PJ301MPort>(Vec(5, 20), module, 0));
    addOutput(createOutput<PJ301MPort>(Vec(85, 20), module, WR_NUM_OUTPUTS));
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
        if (DataRequestFunc)
        {
            auto data = DataRequestFunc();
            auto maxe = *std::max_element(data->begin(),data->end());
            float yscaler = h/maxe;
            float barwidth = w/data->size();
            for (int i=0;i<(int)data->size();++i)
            {
                float xcor = rescale(i,0,data->size()-1,0,w-barwidth);
                float y = (*data)[i]*yscaler;
                if (y>=1.0f)
                {
                    nvgBeginPath(args.vg);
			        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
                    nvgRect(args.vg,xcor,h-y,barwidth,y);
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

MatrixSwitchModule::MatrixSwitchModule()
{
    config(0,18,16,0);
    m_connections.reserve(128);
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
        outputs[i].setVoltage(0.0f);
    }
    // this is very nasty, need to figure out some other way to deal with the thread safety
    if (m_changing_state == true)
    {
        return;
    }
    for (int i=0;i<(int)m_connections.size();++i)
    {
        int src = m_connections[i].m_src;
        int dest = m_connections[i].m_dest;
        float v = outputs[dest].getVoltage();
        v += inputs[src].getVoltage();
        outputs[dest].setVoltage(v);
    }
    
}

json_t* MatrixSwitchModule::dataToJson()
{
    json_t* resultJ = json_object();
    json_t* arrayJ = json_array();
    for (int i=0;i<(int)m_connections.size();++i)
    {
        json_t* conJ = json_object();
        json_object_set(conJ,"src",json_integer(m_connections[i].m_src));
        json_object_set(conJ,"dest",json_integer(m_connections[i].m_dest));
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
        m_changing_state = true;
        m_connections.clear();
        for (int i=0;i<numcons;++i)
        {
            json_t* conJ = json_array_get(arrayJ,i);
            int src = json_integer_value(json_object_get(conJ,"src"));
            int dest = json_integer_value(json_object_get(conJ,"dest"));
            m_connections.emplace_back(src,dest);
        }
        m_changing_state = false;
    }
}

bool MatrixSwitchModule::isConnected(int x, int y)
{
    if (x<0 || x>18)
        return false;
    if (y<0 || y>16)
        return false;
    for (int i=0;i<(int)m_connections.size();++i)
    {
        if (m_connections[i].m_src == x && m_connections[i].m_dest == y)
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
        m_changing_state = true;
        m_connections.emplace_back(x,y);
        m_changing_state = false;
    }
    if (c == false)
    {
        for (int i=0;i<(int)m_connections.size();++i)
        {
            if (m_connections[i].m_src == x && m_connections[i].m_dest == y)
            {
                m_changing_state = true;
                m_connections.erase(m_connections.begin()+i);
                m_changing_state = false;
                return;
            }
        }
    }
}

MatrixSwitchWidget::MatrixSwitchWidget(MatrixSwitchModule* module_)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
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
    nvgFontFaceId(args.vg, g_font->handle);
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
    auto& cons = m_mod->m_connections;
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

RandomClockModule::RandomClockModule()
{
    config(17,1,8);
    configParam(0,0.0f,1.0f,0.1); // master clock density
    float defmult = rescale(1.0f,0.1f,10.0f,0.0f,1.0f);
    for (int i=0;i<8;++i)
    {
        configParam(i+1,0.0f,1.0f,defmult); // clock multiplier
        // gate len
        // >=0.0 && <=0.5 deterministic percentage 1% to 99% of clock interval
        // >0.5 && <=1.0 stochastic distribution favoring short and long values
        configParam(i+9,0.0,1.0f,0.5f); 
    }
}

void RandomClockModule::process(const ProcessArgs& args)
{
    float masterdensity = params[0].getValue();
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
        float multip = rescale(params[i+1].getValue(),0.0f,1.0f,0.1f,10.0f);
        m_clocks[i].setDensity(masterdensity*multip);
        float glen = rescale(params[i+9].getValue(),0.0f,1.0f,0.01,0.99f);
        m_clocks[i].setGateLen(glen);
        outputs[i].setVoltage(10.0f*m_clocks[i].process(args.sampleTime));    
    }
}

RandomClockWidget::RandomClockWidget(RandomClockModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = 100;
    m_mod = m;
    for (int i=0;i<8;++i)
    {
        addOutput(createOutput<PJ301MPort>(Vec(5,30+30*i), module, i));
        addParam(createParam<RoundSmallBlackKnob>(Vec(35, 30+30*i), module, i+1)); 
        addParam(createParam<RoundSmallBlackKnob>(Vec(65, 30+30*i), module, i+9)); 
    }
    addParam(createParam<RoundBlackKnob>(Vec(5, 30+30*8), module, 0));    

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
