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
    m_cur_gain = win;
    if (*m_interpmode == 1)
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

void GrainMixer::processAudio(float* buf, float deltatime)
{
    if (m_inputdur<0.5f)
        return;
    float srcpostouse = 0.0f;
    if (m_playmode == 1)
    {
        srcpostouse = m_region_len*m_scanpos*m_inputdur;
        srcpostouse = m_src_pos_smoother.process(srcpostouse);
    }
    if ((m_random_timing && m_grain_phasor>=m_next_randgrain) ||
        (!m_random_timing && m_grain_phasor>=1.0))
    {
        if (!m_random_timing)
            m_grain_phasor -= 1.0;
        else
            m_grain_phasor = 0.0;
        if (m_random_timing)
        {
            double nextrandpos = -log(m_unidist(m_randgen))/m_grainDensity;
            nextrandpos = clamp(nextrandpos,deltatime*2.0f,1.0f);
            m_next_randgrain = nextrandpos;
        }
        
        if (m_nextLoopStart != m_region_start || m_nextLoopLen != m_region_len)
        {
            m_region_start = m_nextLoopStart;
            m_region_len = m_nextLoopLen;
        }
        ++grainCounter;
        m_outcounter = 0;
        float glen = (1.0f/m_grainDensity) * m_lenMultip;
        glen = clamp(glen,0.01f,4.0f);
        //glen = rescale(glen,0.0f,1.0f,0.02f,0.5f);
        float glensamples = m_sr*glen;
        float posrand = m_gaussdist(m_randgen)*m_posrandamt*m_region_len*m_inputdur;
        if (m_playmode == 0)
            srcpostouse = m_srcpos+posrand;
        
        //if (srcpostouse<0.0f)
        //    srcpostouse = 0.0f;
        //srcpostouse = std::fmod((srcpostouse+m_loopslide*m_looplen)*m_inputdur,m_inputdur);
        m_actSourcePos = srcpostouse+m_region_start*m_inputdur;
        float pan = 0.0f;
        if (grainCounter % 2 == 1)
            pan = 1.0f;
        bool revgrain = m_unidist(m_randgen)<m_reverseProb;
        int availgrain = findFreeGain();
        //float slidedpos = std::fmod(m_srcpos+m_loopslide,1.0f);
        float pitchtouse = m_pitch;
        if (m_polypitches_to_use > 0)
        {
            pitchtouse += m_polypitches[grainCounter % m_polypitches_to_use];
        }
        if (m_pitch_spread>0.0f)
        {
            pitchtouse += m_gaussdist(m_randgen) * std::pow(m_pitch_spread,2.0f) * 12.0f;
            pitchtouse = clamp(pitchtouse,-36.0f,36.0f);
        } else if (m_pitch_spread<0.0f)
        {
            const std::array<float,3> pitchset = {-1.0f,0.0f,1.0f};
            pitchtouse += pitchset[grainCounter % 3] * m_pitch_spread * 12.0f;
        }
        if (availgrain>=0)
        {
            int sourceFrameMin = m_region_start * m_inputdur;
            int sourceFrameMax = sourceFrameMin + (m_region_len * m_inputdur);
            m_grains[availgrain].initGrain(m_inputdur,srcpostouse+m_region_start*m_inputdur,
                glen,pitchtouse,m_sr, pan, revgrain, sourceFrameMin, sourceFrameMax);
        }
        
        m_nextGrainPos=m_sr*(m_grainDensity);
        float sourceSampleRate = m_sources[0]->getSourceSampleRate();
        float rateCompens = sourceSampleRate/m_sr;
        if (m_playmode == 0)
            m_srcpos+=m_sr*((1.0/m_grainDensity))*m_sourcePlaySpeed*rateCompens;
        else    
            m_srcpos = srcpostouse;
        float actlooplen = m_region_len; // std::pow(m_looplen,2.0f);
        float loopend = m_region_start+actlooplen;
        
        if (loopend > 1.0f)
        {
            actlooplen -= loopend - 1.0f;
        }
        
        if (m_srcpos>=actlooplen*m_inputdur)
        {
            m_srcpos = 0.0f;
            m_loop_eoc_pulse.trigger();
        }
        else if (m_srcpos<0.0f)
        {
            m_srcpos = actlooplen*m_inputdur;
            m_loop_eoc_pulse.trigger();
        }
            
        m_actLoopstart = m_region_start;
        m_actLoopend = m_region_start+actlooplen;

    }
    m_loop_eoc_out = 10.0f*(float)m_loop_eoc_pulse.process(deltatime);
    for (int i=0;i<(int)m_grains.size();++i)
    {
        if (m_grains[i].playState==1)
        {
            m_grains[i].process(buf);
        }
    }
    if (debugDivider.process())
    {
        int usedgrains = 0;
        for (int i=0;i<m_grains.size();++i)
        {
            if (m_grains[i].playState == 1)
                ++usedgrains;
        }
        m_grainsUsed = usedgrains;
    }
    ++m_outcounter;
    if (!m_random_timing)
        m_grain_phasor += deltatime * m_grainDensity;
    else
        m_grain_phasor += deltatime;
}


