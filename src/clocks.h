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

