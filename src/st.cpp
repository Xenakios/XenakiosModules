#include "plugin.hpp"
#include <random>

class XStochastic : public rack::Module
{
public:
    XStochastic()
    {

    }
};

class XStochasticWidget : public ModuleWidget
{
public:
    XStochasticWidget(XStochastic* m)
    {
        setModule(m);
    }
};

Model* modelXStochastic = createModel<XStochastic, XStochasticWidget>("XStochastic");
