#include <rack.hpp>
#include "plugin.hpp"
#include <functional>

const int WR_NUM_OUTPUTS = 8;

class WeightedRandomModule : public rack::Module
{
public:
    
    enum paramids
	{
		W_0=0,
        W_1,
        W_2,
        W_3,
        W_4,
        W_5,
        W_6,
        W_7,
        LASTPAR
	};
    WeightedRandomModule();
    void process(const ProcessArgs& args) override;
private:
    dsp::SchmittTrigger m_trig;
    bool m_outcomes[WR_NUM_OUTPUTS];
    float m_cur_discrete_output = 0.0f;
    bool m_in_trig_high = false;
};

class WeightedRandomWidget : public ModuleWidget
{
public:
    WeightedRandomWidget(WeightedRandomModule* mod);
    void draw(const DrawArgs &args) override;
private:

};

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
    HistogramWidget() {}
    void draw(const DrawArgs &args) override;
    
    std::function<std::vector<int>*(void)> DataRequestFunc;
private:
};

class HistogramModuleWidget : public ModuleWidget
{
public:
    HistogramModuleWidget(HistogramModule* mod);
    void draw(const DrawArgs &args) override;
    
private:
    HistogramWidget* m_hwid = nullptr;
};