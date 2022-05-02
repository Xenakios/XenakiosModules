#include "grain_engine.h"

bool ISGrain::initGrain(float inputdur, float startInSource,float len, float pitch, 
        float outsr, float pan, bool reverseGrain, int sourceFrameMin, int sourceFrameMax)
{
    if (playState == 1)
        return false;
    playState = 1;
    m_outpos = 0;
    int inchs = m_syn->getSourceNumChannels();
    float insr = m_syn->getSourceSampleRate();
    float outratio = insr/outsr;
    m_source_phase_inc = outratio * std::pow(2.0,1.0/12*pitch);
    if (reverseGrain)
        m_source_phase_inc = -m_source_phase_inc;
    float* rsinbuf = nullptr;
    int lensamples = outsr*len;
    m_cur_grain_len_samples = lensamples;
    m_grainSize = lensamples;
    m_source_phase = startInSource;
    int srcpossamples = startInSource;
    //srcpossamples+=rack::random::normal()*lensamples;
    srcpossamples = xenakios::clamp((float)srcpossamples,(float)0,inputdur-1.0f);
    
    m_syn->setSubSection(sourceFrameMin, sourceFrameMax);
    m_pan = pan;
    m_inputdur = inputdur;
    return true;
    
}

void ISGrain::process(float* buf)
{
    m_sourceplaypos = 1.0f/m_inputdur*m_source_phase;
    float pangains[2] = {m_pan,1.0f-m_pan};
    int sourceposint = m_source_phase;
    double frac = m_source_phase - sourceposint;
    float dummy = 0.0f;
    float hannpos = 1.0/(m_grainSize-1)*m_outpos;
    hannpos = clamp(hannpos,0.0,1.0f);
    float win = m_hannwind.getValue(hannpos);
    if (m_interpmode == 1)
    {
        for (int i=0;i<m_chans;++i)
        {
            float y0 = m_sinc.call(*m_syn,sourceposint,(1.0-frac),dummy,i);
            y0 *= win * pangains[i];
            buf[i] += y0;
        }
    } else
    {
        for (int i=0;i<m_chans;++i)
        {
            float y0 = m_syn->getBufferSampleSafeAndFade(sourceposint,i,1024);
            float y1 = m_syn->getBufferSampleSafeAndFade(sourceposint+1,i,1024);
            float y2 = y0+(y1-y0) * frac;
            y2 *= win * pangains[i];
            buf[i] += y2;
        }
    }
    
    ++m_outpos;
    m_source_phase += m_source_phase_inc;
    if (m_outpos>=m_grainSize)
    {
        m_outpos = 0;
        playState = 0;
    } 
}