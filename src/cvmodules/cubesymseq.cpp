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
        IN_ORDER_CV,
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
            for (int j=0;j<8;++j)
            {
                m_step_states[i][j] = 0;
            }
        }
            
        configParam(PAR_ORDER,1,24,1,"Step order");
        getParamQuantity(PAR_ORDER)->snapEnabled = true;
        configParam(PAR_SMOOTH,0.0,1.0,0.0,"Output smoothing");
        configParam(PAR_POLYCHANS,1.0,16.0,1.0,"Poly channels");
    }
    float m_cur_permuts[16];
    
    void process(const ProcessArgs& args) override
    {
        int numouts = clamp(inputs[IN_ORDER_CV].getChannels(),1,16);
        bool genoffsets = false;
        int manouts = params[PAR_POLYCHANS].getValue();
        if (manouts>numouts)
        {
            numouts = manouts;
            genoffsets = true;
        }
        outputs[OUT_VOLT].setChannels(numouts);
        
        if (m_reset_trig.process(inputs[IN_RESET].getVoltage()))
        {
            m_cur_step = 0;
            m_eoc_gen.trigger();
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
                float ord = ordbase + rescale(inputs[IN_ORDER_CV].getVoltage(i),-5.0f,5.0f,-12.0,12.0);
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
                    iord = (iord + i) % 24;
                int index = g_permuts[iord][m_cur_step]-1;
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
    float m_cur_outs[16];
    int m_cur_step = 0;
    int m_step_states[16][8];
    dsp::SchmittTrigger m_step_trig;
    dsp::SchmittTrigger m_reset_trig;
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
        
        for (int i=0;i<24;++i)
        {
            nvgBeginPath(args.vg);
            int x = i % 8;
            int y = i / 8;
            int s = m_s->params[CubeSymSeq::PAR_ORDER].getValue()-1;
            if (i == s)
                nvgFillColor(args.vg,nvgRGB(200,200,200));
            else nvgFillColor(args.vg,nvgRGB(0,0,0));
            float xcor = x * m_gridsize;
            float ycor = y * m_gridsize;
            nvgCircle(args.vg,xcor,ycor,m_gridsize/2);
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

class CubeSymSeqWidget : public rack::ModuleWidget
{
public:
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
        addInput(createInput<PJ301MPort>(Vec(50.0, 8*32+40), module, CubeSymSeq::IN_ORDER_CV));
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
