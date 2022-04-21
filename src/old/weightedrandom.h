#pragma once

#include <rack.hpp>
#include "plugin.hpp"
#include <functional>
#include <atomic>

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



