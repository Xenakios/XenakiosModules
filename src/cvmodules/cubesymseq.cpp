#include "../plugin.hpp"

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
        }
    }
    void process(const ProcessArgs& args) override
    {

    }
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
    }
};

Model* modelCubeSymSeq = createModel<CubeSymSeq, CubeSymSeqWidget>("XCubeSymSeq");
