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
    
    bool isConnected(int x, int y);
    void setConnected(int x, int y, bool connect);
    json_t* dataToJson() override;
    void dataFromJson(json_t* root) override;
    std::vector<connection>& getConnections()
    {
        return m_connections[m_activeconnections];
    }
private:
    std::vector<connection>& getAuxCons()
    {
        return m_connections[(m_activeconnections+1) % 2];
    }
    std::atomic<int> m_state{0}; // 0 idle, 1 set up new connections, 2 crossfade
    std::vector<connection> m_connections[2];
    int m_activeconnections = 0;
    int m_crossfadecounter = 0;
    int m_crossfadelen = 44100;
    dsp::ClockDivider m_cd;
    std::vector<float> m_curoutputs;
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

inline float reduce_difference(std::vector<Input>& in, float par_a, float par_b)
{
    float result = in[0].getVoltage();
    for (size_t i=1;i<in.size();++i)
    {
        if (in[i].isConnected())
            result = fabsf(result-in[i].getVoltage());
    }
    return rescale(result*par_a,0.0f,10.0f,-10.0f,10.0f);
}

inline float reduce_and(std::vector<Input>& in, float par_a, float par_b)
{
    //auto maxi = std::numeric_limits<unsigned int>::max();
    unsigned int maxi = 65535;
    unsigned int result = rescale(in[0].getVoltage(),-10.0f,10.0f,0.0,maxi);
    for (size_t i=1;i<in.size();++i)
    {
        if (in[i].isConnected())
        {
            result &= (unsigned int)rescale(in[i].getVoltage(),-10.0f,10.0f,0.0,maxi);
        }
    }
    return rescale(result,0,maxi,-10.0f,10.0f);
}

inline float reduce_or(std::vector<Input>& in, float par_a, float par_b)
{
    //auto maxi = std::numeric_limits<unsigned int>::max();
    unsigned int maxi = 65535;
    unsigned int result = rescale(in[0].getVoltage(),-10.0f,10.0f,0.0,maxi);
    for (size_t i=1;i<in.size();++i)
    {
        if (in[i].isConnected())
        {
            result |= (unsigned int)rescale(in[i].getVoltage(),-10.0f,10.0f,0.0,maxi);
        }
    }
    return rescale(result,0,maxi,-10.0f,10.0f);
}

inline float reduce_xor(std::vector<Input>& in, float par_a, float par_b)
{
    //auto maxi = std::numeric_limits<unsigned int>::max();
    unsigned int maxi = 65535;
    unsigned int result = rescale(in[0].getVoltage(),-10.0f,10.0f,0.0,maxi);
    for (size_t i=1;i<in.size();++i)
    {
        if (in[i].isConnected())
        {
            result ^= (unsigned int)rescale(in[i].getVoltage(),-10.0f,10.0f,0.0,maxi);
        }
    }
    return rescale(result,0,maxi,-10.0f,10.0f);
}

class RoundRobin
{
public:
    RoundRobin() {}
    inline float process(std::vector<Input>& in)
    {
        float r = 0.0f;
        for (int i=0;i<(int)in.size();++i)
        {
            int index = (m_curinput+i) % in.size();
            if (in[index].isConnected())
            {
                r = in[index].getVoltage();
                m_curinput = index+1;
                break;
            }
        }
        return r;
    }
    int m_counter = 0;
    int m_curinput = 0;
    float m_cur_output = 0.0f;
};

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
        ALGO_AND,
        ALGO_OR,
        ALGO_XOR,
        ALGO_DIFFERENCE,
        ALGO_ROUNDROBIN,
        ALGO_LAST
    };
    ReducerModule();
    void process(const ProcessArgs& args) override;
    const char* getAlgoName()
    {
        int algo = params[PAR_ALGO].getValue();
        static const char* algonames[]={"Add","Avg","Mult","Min","Max","And","Or","Xor","Diff","RR"};
        return algonames[algo];
    }
private:
    RoundRobin m_rr;  
};

class ReducerWidget : public ModuleWidget
{
public:
    ReducerWidget(ReducerModule*);
    void draw(const DrawArgs &args) override;
private:
    ReducerModule* m_mod = nullptr;
};

