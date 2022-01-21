#include "../plugin.hpp"
#include "../helperwidgets.h"

const int g_permuts[24][8] =
{
    {1,2,3,4,5,6,7,8},
    {3,5,1,8,6,2,4,7},
    {3,5,1,6,8,2,7,4},
    {2,8,1,3,6,4,7,5},
    {2,8,1,6,3,4,5,7},
    {1,7,4,5,2,3,8,6},
    {1,7,4,2,5,3,6,8},
    {4,6,1,3,8,2,7,5},
    {4,6,1,8,3,2,5,7},
    {1,2,3,4,5,6,7,8},
    {1,3,2,4,5,7,6,8},
    {4,3,2,1,8,7,6,5},
    {1,4,8,5,2,3,7,6},
    {1,8,4,5,2,7,3,6},
    {5,8,4,1,6,7,3,2},
    {3,4,8,7,2,1,5,6},
    {3,8,4,7,2,5,1,6},
    {7,8,4,3,6,5,1,2},
    {3,4,5,6,1,7,2,8},
    {4,8,2,6,1,7,3,5},
    {8,7,1,2,3,5,4,6},
    {1,5,3,7,2,8,4,6},
    {1,4,6,7,2,8,3,5},
    {2,3,5,8,4,6,1,7}

};

class CubeSymSeq : public rack::Module
{
public:
    enum PARAMS
    {
        ENUMS(PAR_VOLTS,8),
        PAR_ORDER,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_TRIG,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_VOLT,
        OUT_EOC,
        OUT_LAST
    };
    CubeSymSeq()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        for (int i=0;i<8;++i)
        {
            configParam(PAR_VOLTS+i,-5.0f,5.0f,0.0f);
            m_cur_outs[i] = 0.0f;
        }
        configParam(PAR_ORDER,1,24,1,"Step order");
        getParamQuantity(PAR_ORDER)->snapEnabled = true;
    }
    void process(const ProcessArgs& args) override
    {
        if (m_step_trig.process(inputs[IN_TRIG].getVoltage()))
        {
            ++m_cur_step;
            if (m_cur_step == 8)
            {
                m_cur_step = 0;
            }
            int ord = params[PAR_ORDER].getValue();
            int index = g_permuts[ord-1][m_cur_step]-1;
            float stepval = params[PAR_VOLTS+index].getValue();
            m_cur_outs[0] = stepval;
        }
        outputs[OUT_VOLT].setVoltage(m_cur_outs[0]);
    }
    float m_cur_outs[8];
    int m_cur_step = 0;
    dsp::SchmittTrigger m_step_trig;
};

class CubeSymSeqWidget : public rack::ModuleWidget
{
public:
    CubeSymSeqWidget(CubeSymSeq* m)
    {
        setModule(m);
        box.size.x = RACK_GRID_WIDTH * 8;
        float xc = 1.0f;
        float yc = 1.0f;
        PortWithBackGround* port = nullptr;
        port = new PortWithBackGround(m,this,CubeSymSeq::OUT_VOLT,xc,yc,"VOLTS",true);
        xc = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,CubeSymSeq::OUT_EOC,xc,yc,"EOC",true);
        xc = port->box.getRight()+2;
        port = new PortWithBackGround(m,this,CubeSymSeq::IN_TRIG,xc,yc,"TRIG",false);
        xc = port->box.getRight()+2;
        for (int i=0;i<8;++i)
        {
            addParam(createParam<RoundBlackKnob>(Vec(1.0, i*32.0f+35.0f), module, CubeSymSeq::PAR_VOLTS+i));
        }
        addParam(createParam<RoundBlackKnob>(Vec(1.0, 8*32.0f+35.0f), module, CubeSymSeq::PAR_ORDER));
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
