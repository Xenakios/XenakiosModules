#pragma once

#include <rack.hpp>
#include "../plugin.hpp"
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

inline float pulse_wave(float frequency, float duty, float phase)
{
    if (duty>=1.0f)
        return 1.0f;
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
            m_cur_output = pulse_wave(divhz,m_gate_len,m_main_phase+m_phase_offset);
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
        m_gate_len = clamp(gl,0.01f,1.0f);
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
    double m_main_phase = 0.0f;
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

