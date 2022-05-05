#pragma once

#include "mischelpers.h"
#include <vector>

class ImgWaveOscillator
{
public:
    void initialise(std::function<float(float)> f, 
    int tablesize)
    {
        m_tablesize = tablesize;
        m_table.resize(tablesize);
        for (int i=0;i<tablesize;++i)
            m_table[i] = f(rack::math::rescale(i,0,tablesize-1,-g_pi,g_pi));
    }
    void setFrequency(float hz)
    {
        m_phaseincrement = m_tablesize*hz*(1.0/m_sr);
        m_freq = hz;
    }
    float getFrequency()
    {
        return m_freq;
    }
    float processSample(float)
    {
        /*
        int index = m_phase;
        float sample = m_table[index];
        m_phase+=m_phaseincrement;
        if (m_phase>=m_tablesize)
            m_phase-=m_tablesize;
        */
        double phasetouse = m_phase;
        /*
        float bitlevels = m_phasebitlevels;
        phasetouse = std::round(phasetouse*bitlevels)/bitlevels;
        */
        int index0 = std::floor(phasetouse);
        int index1 = std::floor(phasetouse)+1;
        if (index1>=m_tablesize)
            index1 = 0;
        float frac = phasetouse-index0;
        float y0 = m_table[index0];
        float y1 = m_table[index1];
        float sample = y0+(y1-y0)*frac;
        m_phase+=m_phaseincrement;
        
        float warp_par = 0.1f;
        
        if (m_phase>=m_tablesize)
            m_phase-=m_tablesize;
        // float normphase = rescale(m_phase,0.0f,m_tablesize-1,0.0f,1.0f);
        // normphase = std::pow(normphase,1.2f);
        // m_phase = rescale(normphase,0.0f,1.0f,0.0f,m_tablesize-1);
        return sample;
    }
    void prepare(int numchans, float sr)
    {
        m_sr = sr;
        setFrequency(m_freq);
    }
    void reset(float initphase)
    {
        m_phase = initphase;
    }
    void setTable(std::vector<float> tb)
    {
        m_tablesize = tb.size();
        m_table = tb;
    }
    void setPhaseWarp(int mode, float par)
    {
        
        m_phasebitlevels = par;
    }
private:
    int m_tablesize = 0;
    std::vector<float> m_table;
    double m_phase = 0.0;
    float m_sr = 44100.0f;
    float m_phaseincrement = 0.0f;
    float m_freq = 440.0f;
    float m_phasebitlevels = 2048.0f;
};
