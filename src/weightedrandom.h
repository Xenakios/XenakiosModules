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

class RandomClock
{
public:
    RandomClock()
    {
        m_cur_interval = generateInterval();
    }
    float process(float timeDelta)
    {
        m_phase+=timeDelta;
        if (m_phase>=m_cur_interval)
        {
            m_clock_high = true;
            m_phase = 0.0f;
            m_cur_interval = generateInterval();
            if (m_gate_len_par>=0.0f && m_gate_len_par<0.5f)
            {
                m_cur_gate_len = rescale(m_gate_len_par,0.0f,0.5f,0.01f,0.99f);
            } else
            {
                // Kumaraswamy distribution
                // (1.0 - ( 1.0 - math.random() ) ^ (1.0/b))^(1.0/a)
                float k_a = 0.3f;
                float k_b = 0.5f;
                float k_x = 1.0f-powf((1.0f-random::uniform()),1.0f/k_b);
                k_x = powf(k_x,1.0f/k_a);
                m_cur_gate_len = rescale(k_x,0.0f,1.0f,0.01,0.99f);
            }
            
        } else
        if (m_phase>=m_cur_interval * m_cur_gate_len)
        {
            m_clock_high = false;
        }
        if (m_clock_high)
            return 1.0f;
        return 0.0f;
    }
    void setDensity(float m)
    {
        m_density = clamp(m,0.01,200.0f);
    }
    void setGateLen(float gl) 
    {
        m_gate_len_par = clamp(gl,0.0f,1.0f);
    }
    float generateInterval()
    {
        // exponential distribution starts from zero and can also produce very large values
        // draw new numbers from it until get a number in a more limited range
        // this should usually only take one iteration or so
        // could also do a simple clamping of the distribution output, but let's be fancy...
        float result = 1.0/m_density;
        int sanitycheck = 0;
        while (true)
        {
            float td = (-log(random::uniform()))/(m_density);
            if (td>=0.005 && td<10.0)
            {
                result = td;
                break;
            }
            ++sanitycheck;
            if (sanitycheck>100)
                break;
        }
        
        return result;
    }
    float getCurrentGateLen() { return m_cur_gate_len; }
private:
    bool m_clock_high = true;
    float m_phase = 0.0f;
    float m_cur_interval = 0.0f;
    float m_density = 1.0f;
    float m_gate_len_par = 0.25f;
    float m_cur_gate_len = 0.5f;
};

class RandomClockModule : public rack::Module
{
public:
    RandomClockModule();
    void process(const ProcessArgs& args) override;
    float m_curDensity = 0.0f;
private:
    RandomClock m_clocks[8];
};

class RandomClockWidget : public ModuleWidget
{
public:
    RandomClockWidget(RandomClockModule*);
    void draw(const DrawArgs &args) override;
private:
    RandomClockModule* m_mod = nullptr;
};

/*
Reduce algorithms :
-Add (mix)
-Average
-Multiply
-Minimum
-Maximum
-...?
*/

inline float reduce_add(std::vector<Input>& in, float par_a, float par_b)
{
    float result = 0.0f;
    for (size_t i=0;i<in.size();++i)
    {
        if (in[i].isConnected())
            result+=in[i].getVoltage();
    }
    return result;
}

inline float reduce_mult(std::vector<Input>& in, float par_a, float par_b)
{
    float result = 1.0f;
    for (size_t i=0;i<in.size();++i)
    {
        if (in[i].isConnected())
            result*=in[i].getVoltage()*par_a;
    }
    return result;
}

inline float reduce_average(std::vector<Input>& in, float par_a, float par_b)
{
    float result = 0.0f;
    int numconnected = 0;
    for (size_t i=0;i<in.size();++i)
    {
        if (in[i].isConnected())
        {
            result+=in[i].getVoltage();
            ++numconnected;
        }
    }
    if (numconnected>0)
        return result/numconnected;
    return 0.0f;
}

inline float reduce_min(std::vector<Input>& in, float par_a, float par_b)
{
    float result = in[0].getVoltage();
    for (size_t i=0;i<in.size();++i)
    {
        if (in[i].isConnected() && in[i].getVoltage()<result)
            result = in[i].getVoltage();
    }
    return result;
}

inline float reduce_max(std::vector<Input>& in, float par_a, float par_b)
{
    float result = in[0].getVoltage();
    for (size_t i=0;i<in.size();++i)
    {
        if (in[i].isConnected() && in[i].getVoltage()>result)
            result = in[i].getVoltage();
    }
    return result;
}

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
        ALGO_LAST
    };
    ReducerModule();
    void process(const ProcessArgs& args) override;
    
private:
    
};

class ReducerWidget : public ModuleWidget
{
public:
    ReducerWidget(ReducerModule*);
    void draw(const DrawArgs &args) override;
private:
    ReducerModule* m_mod = nullptr;
};

