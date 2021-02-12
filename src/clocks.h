#pragma once

#include <rack.hpp>
#include "plugin.hpp"
#include <functional>
#include <atomic>

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
    float getCurrentInterval() 
    { 
        return clamp(m_cur_interval,0.0f,10.0f);
    } 
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
    enum PARAMS
    {
        PAR_MASTER_DENSITY,
        ENUMS(PAR_DENSITY_MULTIP, 8),
        ENUMS(PAR_GATE_LEN, 8),
        PAR_LAST
    };
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

inline float pulse_wave(float frequency, float duty, float phase)
{
    phase = std::fmod(phase*frequency,1.0f);
    if (phase<duty)
        return 1.0f;
    return 0.0f;
}

class DividerClock
{
public:
    DividerClock() 
    {
        m_cd.setDivision(32);
    }
#ifdef XOLD_CLOCK_CODE
    float process(float timedelta)
    {
        float divlen = m_main_len/m_division;
        m_main_phase+=timedelta;
        if (m_main_phase>=m_main_len)
        {
            if (m_set_next_division>0)
            {
                m_division = m_set_next_division;
                m_set_next_division = -1;
                divlen = m_main_len/m_division;
            }
            if (m_set_next_main_len>0.0f)
            {
                m_main_len = m_set_next_main_len;
                m_set_next_main_len = -1.0f;
                divlen = m_main_len/m_division;
            }
            if (m_set_next_offset>=0.0f)
            {
                m_phase_offset = m_set_next_offset;
                m_set_next_offset = -1.0f;
            }
            m_main_phase = 0.0f;
            m_div_counter = 0;
            m_div_next = divlen;
            m_clock_high = true;
        }
        if (m_main_phase>=m_div_next)
        {
            m_clock_high = true;
            ++m_div_counter;
            m_div_next=m_div_counter*divlen;
        }
        else if (m_main_phase>=m_div_next-divlen*(1.0-m_gate_len))
        {
            m_clock_high = false;
        }
        if (m_clock_high)
            return 1.0f;
        return 0.0f;
    }
#else
    float process(float timedelta)
    {
        m_main_phase+=timedelta;
        if (m_main_phase>=m_main_len)
        {
            m_main_phase = 0.0f;
        }
        //if (m_cd.process())
        {
            float divhz = 1.0f/(m_main_len/m_division);
            m_cur_output = pulse_wave(divhz,m_gate_len,m_main_phase+m_phase_offset*m_main_len);
        }
        return m_cur_output;
    }
#endif
    void setParams(float mainlen, float div, float offset,bool immediate)
    {
        //if (immediate)
        {
            m_main_len = mainlen;
            m_division = div;
            m_phase_offset = rescale(offset,0.0f,1.0f,0.0f,m_main_len);
            return;
        }
        if (mainlen!=m_main_len)
            m_set_next_main_len = mainlen;
        if (div!=m_division)
            m_set_next_division = div;
        if (offset!=m_set_next_offset)
            m_set_next_offset = clamp(offset,0.0f,0.99f);
    }
    void setGateLen(float gl)
    {
        m_gate_len = clamp(gl,0.01f,0.99f);
    }
    void reset()
    {
        float divlen = m_main_len/m_division;
        m_main_phase = 0.0f;
        m_div_counter = 0;
        m_div_next = divlen;
        m_clock_high = true;
    }
    float mainClockHigh()
    {
        if (m_main_phase>=0.0f && m_main_phase<0.01)
            return 1.0f;
        return 0.0f;
    }
private:
    float m_main_len = 1.0f;
    float m_division = 1.0f;
    float m_main_phase = 0.0f;
    float m_phase_offset = 0.0f;
    float m_div_next = 0.0f;
    int m_div_counter = 0;
    
    float m_gate_len = 0.5f;
    bool m_clock_high = true;
    float m_set_next_main_len = -1.0f;
    int m_set_next_division = -1;
    float m_set_next_offset = -1.0f;
    dsp::ClockDivider m_cd;
    float m_cur_output = 0.0f;
};

class DivisionClockModule : public rack::Module
{
public:
    
    DivisionClockModule()
    {
        m_cd.setDivision(128);
        config(33,25,16);
        for (int i=0;i<8;++i)
            configParam(i,1.0f,32.0f,4.0f,"Main div");
        for (int i=0;i<8;++i)
            configParam(8+i,1.0f,32.0f,1.0f,"Sub div");
        for (int i=0;i<8;++i)
            configParam(16+i,0.01f,0.99f,0.5f,"Gate len");
        for (int i=0;i<8;++i)
            configParam(24+i,0.0f,1.0f,0.0f,"Phase offset");
        configParam(32,30.0f,240.0f,60.0f,"BPM");
    }
    void process(const ProcessArgs& args) override
    {
        
        if (m_reset_trig.process(inputs[0].getVoltage()))
        {
            for (int i=0;i<8;++i)
            {
                m_clocks[i].reset();
            }
        }
        const float bpm = params[32].getValue();
        bool updateparams = m_cd.process();
        for (int i=0;i<8;++i)
        {
            //if (outputs[i].isConnected() || outputs[i+8].isConnected())
            {
                if (updateparams)
                {
                    float v = params[i].getValue()+rescale(inputs[i+1].getVoltage(),0.0,10.0f,0.0,31.0f);
                    v = clamp(v,1.0,32.0);
                    float len = 60.0f/bpm/4.0f*v;
                    v = params[i+8].getValue()+rescale(inputs[i+9].getVoltage(),0.0,10.0f,0.0,31.0f);
                    v = clamp(v,1.0,32.0);
                    float div = v;
                    float offs = params[i+24].getValue();
                    m_clocks[i].setParams(len,div,offs,false);
                    v = params[i+16].getValue()+rescale(inputs[i+17].getVoltage(),0.0,10.0f,0.0,0.99f);
                    m_clocks[i].setGateLen(v); // clamped in the clock method
                }
                outputs[i].setVoltage(m_clocks[i].process(args.sampleTime)*10.0f);
                outputs[i+8].setVoltage(m_clocks[i].mainClockHigh()*10.0f);
            }
        }
        
    }
private:
    DividerClock m_clocks[8];
    dsp::SchmittTrigger m_reset_trig;
    dsp::ClockDivider m_cd;
};

class DividerClockWidget : public ModuleWidget
{
public:
    DividerClockWidget(DivisionClockModule* m);
    void draw(const DrawArgs &args) override;
    
};