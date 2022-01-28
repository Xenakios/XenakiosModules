#include "../plugin.hpp"
#include "../helperwidgets.h"

// taken from Xenakis Formalized music, so should hopefully be right...?

const int g_permuts[24][8] =
{
    {1,2,3,4,5,6,7,8},
    {2,1,4,3,6,5,8,7},
    {3,4,1,2,7,8,5,6},
    {4,3,2,1,8,7,6,5},
    {2,3,1,4,6,7,5,8},
    {3,1,2,4,7,5,6,8},
    {2,4,3,1,6,8,7,5},
    {4,1,3,2,8,5,7,6},
    {3,2,4,1,7,6,8,5},
    {4,2,1,3,8,6,5,7},
    {1,3,4,2,5,7,8,6},
    {1,4,2,3,5,8,6,7},
    {7,8,6,5,3,4,2,1},
    {7,6,5,8,3,2,1,4},
    {8,6,7,5,4,2,3,1},
    {6,7,8,5,2,3,4,1},
    {6,8,5,7,2,4,1,3},
    {6,5,7,8,2,1,3,4},
    {8,7,5,6,4,3,1,2},
    {7,5,8,6,3,1,4,2},
    {5,8,7,6,1,4,3,2},
    {5,7,6,8,1,3,2,4},
    {8,5,6,7,4,1,2,3},
    {5,6,8,7,1,2,4,3}
};

const int g_call_changes[24][8] =
{
    {1,2,3,4,5,6,7,8},
    {1,2,3,4,5,7,6,8},
    {1,2,3,4,7,5,6,8},
    {1,2,3,7,4,5,6,8},
    {1,2,7,3,4,5,6,8},
    {1,2,7,3,5,4,6,8},
    
    {1,2,7,5,3,4,6,8},
    {1,2,5,7,3,4,6,8},
    {1,2,5,3,7,4,6,8},
    {1,2,3,5,7,4,6,8},
    {1,3,2,5,7,4,6,8},
    {1,3,5,2,7,4,6,8},
    
    {1,3,5,7,2,4,6,8},
    {1,5,3,7,2,4,6,8},
    {1,5,3,2,7,4,6,8},
    {1,5,2,3,7,4,6,8},
    {1,5,2,3,7,6,4,8},
    {1,5,2,3,6,7,4,8},
    
    {1,5,2,6,3,7,4,8},
    {1,2,5,6,3,7,4,8},
    {1,2,5,3,6,7,4,8},
    {1,2,3,5,6,7,4,8},
    {1,2,3,5,6,4,7,8},
    {1,2,3,5,4,6,7,8}
};

unsigned int g_css_seed = 1;

class CubeSymSeq : public rack::Module
{
public:
    enum PARAMS
    {
        ENUMS(PAR_VOLTS,8),
        PAR_ORDER,
        PAR_SMOOTH,
        PAR_POLYCHANS,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_TRIG,
        IN_MULTIPURPOSE1,
        IN_RESET,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_VOLT,
        OUT_EOC,
        OUT_LAST
    };
    enum LIGHTS
    {
        ENUMS(LIGHT_ACTSTEP,8),
        LIGHT_PENDING_CHANGE,
        LIGHT_LAST
    };
    CubeSymSeq()
    {
        ++g_css_seed;
        config(PAR_LAST,IN_LAST,OUT_LAST,LIGHT_LAST);
        for (int i=0;i<8;++i)
        {
            configParam(PAR_VOLTS+i,-5.0f,5.0f,0.0f);
        }
        for (int i=0;i<16;++i)
        {
            m_cur_outs[i] = 0.0f;
            m_slews[i].setAmount(0.999);
            m_cur_permuts[i] = 1.0f;
            m_cur_ipermuts[i] = 0;
            for (int j=0;j<8;++j)
            {
                m_step_states[i][j] = 0;
            }
        }
            
        configParam(PAR_ORDER,1,24,1,"Step order");
        getParamQuantity(PAR_ORDER)->snapEnabled = true;
        getParamQuantity(PAR_ORDER)->randomizeEnabled = false;
        configParam(PAR_SMOOTH,0.0,1.0,0.0,"Output smoothing");
        getParamQuantity(PAR_SMOOTH)->randomizeEnabled = false;
        configParam(PAR_POLYCHANS,1.0,16.0,1.0,"Poly channels");
        getParamQuantity(PAR_POLYCHANS)->snapEnabled = true;
        getParamQuantity(PAR_POLYCHANS)->randomizeEnabled = false;
        reShuffle();
    } 
    void reShuffle()
    {
        std::array<int,24> temp;
        std::iota(temp.begin(),temp.end(),0);
        std::shuffle(temp.begin()+1,temp.end(),m_rng);
        std::copy(temp.begin(),temp.begin()+16,m_rand_offsets.begin());
    }  
    std::minstd_rand m_rng{g_css_seed};
    float m_cur_permuts[16];
    int m_cur_num_outs = 0;
    int m_cur_ipermuts[16];
    
    std::array<int,16> m_rand_offsets;
    int m_polyoffset_algo = 0;
    void process(const ProcessArgs& args) override
    {
        int numouts = clamp(inputs[IN_MULTIPURPOSE1].getChannels(),1,16);
        bool genoffsets = false;
        int manouts = params[PAR_POLYCHANS].getValue();
        if (manouts>numouts)
        {
            numouts = manouts;
            genoffsets = true;
        }
        m_cur_num_outs = numouts;
        outputs[OUT_VOLT].setChannels(numouts);
        
        if (m_reset_trig.process(inputs[IN_RESET].getVoltage()))
        {
            m_cur_step = 0;
            m_eoc_gen.trigger();
        }
        if (m_in_cv_mode > 0)
        {
            
            if (m_adv_trig.process(inputs[IN_MULTIPURPOSE1].getVoltage()))
            {
                float pardelta = 0.0f;
                float oparval = params[PAR_ORDER].getValue();
                if (m_in_cv_mode == 1)  
                    oparval += 1.0f;
                if (m_in_cv_mode == 2)  
                    oparval -= 1.0f;
                if (oparval>24.0f)
                    oparval = 1.0f;
                else if (oparval<1.0f)
                    oparval = 24.0f;
                params[PAR_ORDER].setValue(oparval);
            }
                
        }
        if (m_step_trig.process(inputs[IN_TRIG].getVoltage()))
        {
            for (int i=0;i<16;++i)
            {
                for (int j=0;j<8;++j)
                {
                    m_step_states[i][j] = 0;
                }
            }
            float ordbase = params[PAR_ORDER].getValue();
            ++m_cur_step;
            if (m_cur_step == 8)
            {
                m_cur_step = 0;
                m_eoc_gen.trigger();
                
            }
            for (int i=0;i<numouts;++i)
            {
                float ord = ordbase;
                if (m_in_cv_mode == 0)
                    ord += rescale(inputs[IN_MULTIPURPOSE1].getVoltage(i),-5.0f,5.0f,-12.0,12.0);
                ord = clamp(ord,1.0f,24.0f);
                
                if (ord!=m_cur_permuts[i])
                {
                    if (m_cur_step == 0)
                    {
                        lights[LIGHT_PENDING_CHANGE].setBrightness(0.0f);
                        m_cur_permuts[i] = ord;
                    } else
                    {
                        lights[LIGHT_PENDING_CHANGE].setBrightness(1.0f);
                    }
                    
                } 
                int iord = (int)m_cur_permuts[i]-1;
                
                if (genoffsets) // generate poly permutation number offsets for manual poly count
                {
                    if (m_polyoffset_algo == 0)
                        iord = (iord + i) % 24;
                    else if (m_polyoffset_algo == 1)
                        iord = (iord + m_rand_offsets[i]) % 24;
                }
                    
                m_cur_ipermuts[i] = iord;
                int index = 0; 
                int whichtable = 0;
                if (whichtable == 0)
                    index = g_permuts[iord][m_cur_step]-1;
                else
                    index = g_call_changes[iord][m_cur_step]-1;
                float stepval = params[PAR_VOLTS+index].getValue();
                m_cur_outs[i] = stepval;
                for (int j=0;j<8;++j)
                {
                    if (j == index)
                    {
                        m_step_states[i][j] = 1;
                    } 
                }
            }
            
            
        }
        float samt = rescale(params[PAR_SMOOTH].getValue(),0.0f,1.0f,0.99f,0.9995f);
        for (int i=0;i<numouts;++i)
        {
            m_slews[i].setAmount(samt);
            if (samt>0.99f)
                outputs[OUT_VOLT].setVoltage(m_slews[i].process(m_cur_outs[i]),i);
            else outputs[OUT_VOLT].setVoltage(m_cur_outs[i],i);
        }
        
        float eocv = (float)m_eoc_gen.process(args.sampleTime)*10.0f;
        outputs[OUT_EOC].setVoltage(eocv);
    }
    json_t* dataToJson() override
    {
        json_t* resultJ = json_object();
        json_object_set(resultJ,"poa",json_integer(m_polyoffset_algo));
        std::stringstream ss;
        ss << m_rng;
        json_object_set(resultJ,"rngstate",json_string(ss.str().c_str()));
        return resultJ;
    }
    void dataFromJson(json_t* root) override
    {
        if (auto j = json_object_get(root,"poa")) m_polyoffset_algo = json_integer_value(j);
        if (auto j = json_object_get(root,"rngstate"))
        {
            std::string s = json_string_value(j);
            std::stringstream ss;
            ss << s;
            ss >> m_rng;
            reShuffle();
        }
    }
    float m_cur_outs[16];
    
    int m_cur_step = 0;
    int m_step_states[16][8];
    dsp::SchmittTrigger m_step_trig;
    dsp::SchmittTrigger m_reset_trig;
    dsp::SchmittTrigger m_adv_trig;
    int m_in_cv_mode = 1;
    dsp::PulseGenerator m_eoc_gen;
    OnePoleFilter m_slews[16];
};

class CSSButtonGroupWidget : public rack::TransparentWidget
{
public:
    CSSButtonGroupWidget(CubeSymSeq* s) : m_s(s)
    {

    }
    float m_gridsize = 10.0f;
    void onButton(const event::Button& e) override
    {
        int x = e.pos.x / m_gridsize;
        int y = e.pos.y / m_gridsize;
        int permut = clamp(y*8+x,0,23);
        m_s->params[CubeSymSeq::PAR_ORDER].setValue(permut+1);
    }
    void draw(const DrawArgs &args) override
    {
        if (m_s==nullptr)
            return;
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg,nvgRGB(0,0,0));
        nvgRect(args.vg,0,0,box.size.x,box.size.y);
        nvgFill(args.vg);
        for (int i=0;i<24;++i)
        {
            nvgBeginPath(args.vg);
            int x = i % 8;
            int y = i / 8;
            int s = m_s->params[CubeSymSeq::PAR_ORDER].getValue()-1;
            if (i == s)
                nvgFillColor(args.vg,nvgRGB(200,200,200));
            else nvgFillColor(args.vg,nvgRGB(50,50,50));
            float xcor = x * m_gridsize;
            float ycor = y * m_gridsize;
            nvgCircle(args.vg,xcor+m_gridsize/2,ycor+m_gridsize/2,m_gridsize/2-1);
            nvgFill(args.vg);
            
        }
        
        for (int i=0;i<m_s->m_cur_num_outs;++i)
        {
            int perm = m_s->m_cur_ipermuts[i];
            int x = perm % 8;
            int y = perm / 8;
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg,nvgRGB(0,255,255));
            float xcor = x * m_gridsize;
            float ycor = y * m_gridsize;
            nvgCircle(args.vg,xcor+m_gridsize/2,ycor+m_gridsize/2,m_gridsize/2-3);
            nvgFill(args.vg);
        }
        nvgRestore(args.vg);
    }
private:
    CubeSymSeq* m_s = nullptr;
};

class CSSStepsWidget : public rack::TransparentWidget
{
public:
    
    CSSStepsWidget(CubeSymSeq* s, int row) : m_mod(s), m_row(row)
    {
        box.size.x = 16 * 3;
        box.size.y = 5;
    }
    void draw(const DrawArgs &args) override
    {
        if (m_mod == nullptr)
            return;
        nvgSave(args.vg);
        for (int i=0;i<16;++i)
        {
            
            int s = m_mod->m_step_states[i][m_row];
            nvgBeginPath(args.vg);
            if (s == 0)
                nvgFillColor(args.vg,nvgRGB(0,0,0));
            else nvgFillColor(args.vg,nvgRGB(0,255,0));
            nvgCircle(args.vg,i*6.5f,0.0f,3.0f);
            nvgFill(args.vg);
            
        }
        nvgRestore(args.vg);
    }
private:
    CubeSymSeq* m_mod = nullptr;
    int m_row = 0;
};

struct MyFloatQuantity : Quantity 
{
    MyFloatQuantity(std::string lab, float minval, float maxval, float defval,std::function<void(float)> setValueCallback) :
        m_lab(lab), m_minval(minval),m_maxval(maxval),m_defval(defval),svCallback(setValueCallback)
    {
        m_value = defval;
    }   
    std::string m_lab;
    float m_minval = 0.0f;
    float m_maxval = 1.0f;
    float m_defval = 0.0f;   
    float m_value = 0.0f;                
    std::function<void(float)> svCallback;
    void setValue(float value) override 
    {
        m_value = clamp(value,m_minval,m_maxval);
        if (svCallback)
            svCallback(m_value);    
    }
    float getValue() override {
        return m_value;
    }
    float getDefaultValue() override {
        return m_defval;
    }
    float getDisplayValue() override {
        return getValue();
    }
    void setDisplayValue(float displayValue) override {
        setValue(displayValue);
    }
    std::string getLabel() override {
        return m_lab;
    }
    std::string getUnit() override {
        return "";
    }
    int getDisplayPrecision() override {
        return 3;
    }
    float getMaxValue() override {
        return m_maxval;
    }
    float getMinValue() override {
        return m_minval;
    }
};

struct MyValueSlider : ui::Slider
{
    MyValueSlider(std::string lab, float minval, float maxval, float defval, std::function<void(float)> cb)
    {
        this->quantity = new MyFloatQuantity(lab,minval,maxval,defval,cb);
    }
    ~MyValueSlider()
    {
        delete this->quantity;
    }
};

class CubeSymSeqWidget : public rack::ModuleWidget
{
public:
    float m_gen_minv = -5.0f;
    float m_gen_maxv = 5.0f;
    float m_gen_shape = 1.0f;
    void appendContextMenu(Menu* menu) override
    {
        menu->addChild(new MenuSeparator);
        auto slid = new MyValueSlider("Lowest value",-5.0f,5.0f,m_gen_minv,[this](float x){ m_gen_minv = x; });
        slid->box.size = {150.0f,20.0f};
        menu->addChild(slid);
        slid = new MyValueSlider("Highest value",-5.0f,5.0f,m_gen_maxv,[this](float x){ m_gen_maxv = x; });
        slid->box.size = {150.0f,20.0f};
        menu->addChild(slid);
        CubeSymSeq* sm = dynamic_cast<CubeSymSeq*>(module);
        menu->addChild(createMenuItem([this,sm]()
        {
            for (int i=0;i<8;++i)
            {
                float norm = rescale((float)i,0,7,0.0f,1.0f);
                float val = rescale(norm,0.0f,1.0f,m_gen_minv,m_gen_maxv);
                sm->params[CubeSymSeq::PAR_VOLTS+i].setValue(val);
            }
        },"Generate step values"));
        menu->addChild(createMenuItem([sm]()
        {
            if (sm->m_polyoffset_algo == 0)
                sm->m_polyoffset_algo = 1;
            else sm->m_polyoffset_algo = 0;
        },"Randomized polyphonic permutations", CHECKMARK(sm->m_polyoffset_algo == 1) ));
        menu->addChild(createMenuItem([sm]()
        {
            sm->reShuffle();
        },"Re-randomize polyphonic permutations"));
    }
    CubeSymSeqWidget(CubeSymSeq* m)
    {
        setModule(m);
        box.size.x = RACK_GRID_WIDTH * 10;
        float xc = 1.0f;
        float yc = 1.0f;
        PortWithBackGround* port = nullptr;
        port = new PortWithBackGround(m,this,CubeSymSeq::OUT_VOLT,xc,yc,"VOLTS",true);
        xc = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,CubeSymSeq::OUT_EOC,xc,yc,"EOC",true);
        xc = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,CubeSymSeq::IN_TRIG,xc,yc,"TRIG",false);
        xc = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,CubeSymSeq::IN_RESET,xc,yc,"RESET",false);
        xc = port->box.getRight()+2;
        for (int i=0;i<8;++i)
        {
            addParam(createParam<RoundBlackKnob>(Vec(1.0, i*32.0f+40.0f), module, CubeSymSeq::PAR_VOLTS+i));
            //LightWidget* lw;
            //addChild(lw = createLight<GreenLight>(Vec(32.0, i*32.0f+43.0f),module,CubeSymSeq::LIGHT_ACTSTEP+i));
            //lw->box.size = {6.0f,6.0f};
            if (m)
            {
                auto w = new CSSStepsWidget(m,i);
                addChild(w);
                w->box.pos = {32.0f,(float)i*32+46};
                //w->box.size = {16*8,8};
            }
            
        }
        RoundBigBlackKnob* knob;
        addParam(knob = createParam<RoundBigBlackKnob>(Vec(1.0, 8*32.0f+40.0f), module, CubeSymSeq::PAR_ORDER));
        LightWidget* lw;
        addChild(lw = createLight<RedLight>(Vec(50.0, 8*32.0f+70.0f),module,CubeSymSeq::LIGHT_PENDING_CHANGE));
        lw->box.size = {6.0f,6.0f};
        addInput(createInput<PJ301MPort>(Vec(50.0, 8*32+40), module, CubeSymSeq::IN_MULTIPURPOSE1));
        addParam(createParam<RoundBlackKnob>(Vec(85.0, 8*32.0f+40.0f), module, CubeSymSeq::PAR_SMOOTH));
        addParam(createParam<RoundBlackKnob>(Vec(85.0, 8*32.0f+70.0f), module, CubeSymSeq::PAR_POLYCHANS));
        auto butgr = new CSSButtonGroupWidget(m);
        addChild(butgr);
        butgr->box.size = {80.0f,30.0f};
        butgr->box.pos = {knob->box.getLeft()+5,knob->box.getBottom()+5};
    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x50, 0x50, 0x50, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelCubeSymSeq = createModel<CubeSymSeq, CubeSymSeqWidget>("XCubeSymSeq");
