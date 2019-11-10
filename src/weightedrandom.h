#pragma once

#include <rack.hpp>
#include "plugin.hpp"
#include <functional>
#include <atomic>

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

class MatrixSwitchModule : public rack::Module
{
public:
    struct connection
    {
        connection() {}
        connection(int s,int d) : m_src(s), m_dest(d) {}
        int m_src = -1;
        int m_dest = -1;
    };
    MatrixSwitchModule();
    void process(const ProcessArgs& args) override;
    std::vector<connection> m_connections;
    bool isConnected(int x, int y);
    void setConnected(int x, int y, bool connect);
    json_t* dataToJson() override;
    void dataFromJson(json_t* root) override;
private:
    std::atomic<bool> m_changing_state{false};
};

class MatrixGridWidget : public TransparentWidget
{
public:
    MatrixGridWidget(MatrixSwitchModule*);
    void onButton(const event::Button& e) override;
    void draw(const DrawArgs &args) override;
private:
    MatrixSwitchModule* m_mod = nullptr;
};

class MatrixSwitchWidget : public ModuleWidget
{
public:
    MatrixSwitchWidget(MatrixSwitchModule*);

    void draw(const DrawArgs &args) override;
};

