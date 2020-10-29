#pragma once

#include <rack.hpp>
#include "plugin.hpp"
#include <functional>
#include <atomic>
#include <random>

inline double custom_log(double value, double base)
{
    return std::log(value)/std::log(base);
}

inline void sanitizeRange(float& a, float& b, float mindiff)
{
	if (b<a)
		std::swap(a,b);
	if (b-a<mindiff)
		b+=mindiff;
}

enum Distributions
	{
		DIST_Uniform,
		DIST_Gauss,
		DIST_Cauchy,
		LASTDIST
	};
enum ResetModes
{
	RM_Zeros,
	RM_Avg,
	RM_Min,
	RM_Max,
	RM_UniformRandom,
	RM_BinaryRandom,
	LASTRM
};




inline float avg(float a, float b)
{
	return a + (b - a) / 2.0f;
}

class GendynNode
{
public:
	GendynNode() {}
	float m_x_prim = 0.0f;
	float m_y_prim = 0.0f;
	float m_x_sec = 0.0f;
	float m_y_sec = 0.0f;
};

class GendynOsc
{
public:
	GendynOsc()
	{
		m_nodes.resize(128);
		for (int i = 0; i < 128; ++i)
		{
			m_nodes[i].m_x_prim = avg(m_time_primary_high_barrier, m_time_primary_high_barrier);
			m_nodes[i].m_x_sec = avg(m_time_secondary_low_barrier, m_time_secondary_high_barrier);

		}
		m_cur_dur = m_nodes.front().m_x_sec;
		m_cur_y0 = m_nodes.front().m_y_sec;
		m_cur_y1 = m_nodes[1].m_y_sec;
		m_next_segment_time = m_nodes[0].m_x_sec;
	}
	void setRandomSeed(int s)
	{
		m_rand = std::mt19937(s);
	}
	void process(float* buf, int nframes)
	{
		for (int i = 0; i < nframes; ++i)
		{
			float t1 = m_cur_dur;
			
			float y0 = m_cur_y0;
			float y1 = m_cur_y1;
			float s = y0 + (y1 - y0) / t1 * m_segment_phase;
			buf[i] = s;
			m_phase += 1.0;
			m_segment_phase += 1.0;
			if (m_phase >= m_next_segment_time)
			{
				++m_cur_node;
				
				if (m_cur_node < m_num_segs - 1)
				{
					m_cur_dur = m_nodes[m_cur_node].m_x_sec;
					m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
					m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
					m_next_segment_time += m_cur_dur;
				}
				if (m_cur_node == m_num_segs - 1)
				{
					m_cur_dur = m_nodes[m_cur_node].m_x_sec;
					m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
					m_next_segment_time += m_cur_dur;
					updateTable();
					m_cur_y1 = m_nodes.front().m_y_sec;
				}
				if (m_cur_node == m_num_segs)
				{
					
					m_cur_node = 0;
					m_cur_dur = m_nodes[m_cur_node].m_x_sec;
					m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
					m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
					m_next_segment_time += m_cur_dur;
					m_phase = 0.0;
				}
				m_segment_phase = 0.0;
			}
		}
	}
	void resetTable()
	{
		std::uniform_real_distribution<float> ampdist{m_amp_secondary_low_barrier,m_amp_secondary_high_barrier};
		std::uniform_real_distribution<float> timedist{m_time_secondary_low_barrier,m_time_secondary_high_barrier};
		std::uniform_real_distribution<float> unidist(0.0,1.0);
		for (int i = 0; i < m_num_segs; ++i)
		{
			m_nodes[i].m_x_prim = avg(m_time_primary_low_barrier,m_time_primary_high_barrier);
			if (m_timeResetMode == RM_Avg)
				m_nodes[i].m_x_sec = avg(m_time_secondary_low_barrier,m_time_secondary_high_barrier);
			else if (m_timeResetMode == RM_BinaryRandom)
			{
				if (unidist(m_rand)<0.5)
					m_nodes[i].m_x_sec = m_time_secondary_low_barrier;
				else m_nodes[i].m_x_sec = m_time_secondary_high_barrier;
			}
			else
			{
				m_nodes[i].m_x_sec = m_sampleRate/m_center_frequency/m_num_segs;
			}
			m_nodes[i].m_y_prim = 0.0f;
			if (m_ampResetMode == RM_Zeros)
				m_nodes[i].m_y_sec = 0.0f;
			else if (m_ampResetMode == RM_UniformRandom)
				m_nodes[i].m_y_sec = ampdist(m_rand);
			else
				m_nodes[i].m_y_sec = 0.0f;
		}
		m_cur_node = 0;
        m_phase = 0.0;
		m_next_segment_time = m_nodes[0].m_x_sec;
		m_segment_phase = 0.0;
		m_cur_dur = m_nodes[m_cur_node].m_x_sec;
		m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
		m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
	}
	void setFrequencies(float center, float a, float b)
	{
		m_center_frequency = center;
		float hz = center*pow(2.0,(1.0/12.0*a));
		m_low_frequency = hz;
		m_time_secondary_high_barrier = clamp(m_sampleRate/hz/m_num_segs,1.0f,128.0f);
		hz = center*pow(2.0,(1.0/12.0*b));
		m_high_frequency = hz;
		m_time_secondary_low_barrier = clamp(m_sampleRate/hz/m_num_segs,1.0,128.0f);
		sanitizeRange(m_time_secondary_low_barrier,m_time_secondary_high_barrier,1.0f);
	}
	void updateTable()
	{
		std::normal_distribution<float> timedist(m_time_mean, m_time_dev);
		std::normal_distribution<float> ampdist(m_amp_mean, m_amp_dev);
		float segAcc = 0.0f;
		for (int i = 0; i < m_num_segs; ++i)
		{
			float x_p = m_nodes[i].m_x_prim;
			x_p += timedist(m_rand);
			x_p = reflect_value(m_time_primary_low_barrier, x_p,m_time_primary_high_barrier);
			float x_s = m_nodes[i].m_x_sec;
			x_s += x_p;
			float secbar0 = m_time_secondary_low_barrier;
			float secbar1 = m_time_secondary_high_barrier;
			sanitizeRange(secbar0,secbar1,1.0f);
			x_s = reflect_value(secbar0, x_s, secbar1);
			m_nodes[i].m_x_prim = x_p;
			m_nodes[i].m_x_sec = x_s;
			segAcc+=m_nodes[i].m_x_sec;
			float y_p = m_nodes[i].m_y_prim;
			y_p += ampdist(m_rand);
			y_p = clamp(y_p,m_amp_primary_low_barrier, m_amp_primary_high_barrier);
			float y_s = m_nodes[i].m_y_sec;
			y_s += y_p;
			y_s = clamp(y_s,m_amp_secondary_low_barrier, m_amp_secondary_high_barrier);
			m_nodes[i].m_y_prim = y_p;
			m_nodes[i].m_y_sec = y_s;
		}
		float freq = m_sampleRate/segAcc;
		float volts = custom_log(freq/rack::dsp::FREQ_C4,2.0f);
        m_curFrequencyVolts = clamp(volts,-5.0,5.0);
		m_next_segment_time = m_nodes[0].m_x_sec;
	}
	int m_num_segs = 11;
	float m_time_primary_low_barrier = -1.0;
	float m_time_primary_high_barrier = 1.0;
	float m_time_secondary_low_barrier = 5.0;
	float m_time_secondary_high_barrier = 20.0;
	float m_time_mean = 0.0f;
	float m_time_dev = 0.01;
	float m_amp_primary_low_barrier = -0.01;
	float m_amp_primary_high_barrier = 0.01;
	float m_amp_secondary_low_barrier = -0.2;
	float m_amp_secondary_high_barrier = 0.2;
	float m_amp_mean = 0.0f;
	float m_amp_dev = 0.01;
	int m_timeResetMode = RM_Avg;
	int m_ampResetMode = RM_Zeros;
	float m_center_frequency = 440.0f;
	float m_low_frequency = 440.0f;
	float m_high_frequency = 440.0f;
	float m_curFrequencyVolts = 0.0f;
    void setNumSegments(int n)
    {
        if (n!=m_num_segs)
        {
            m_num_segs = clamp(n,3,64);
            //if (m_num_segs>=m_cur_node)
			{
				m_cur_node = 0;
            	m_phase = 0.0;
				m_cur_dur = m_nodes[m_cur_node].m_x_sec;
				m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
				m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
			}
        }
    }
	float m_sampleRate = 44100.0f;
	
private:
	int m_cur_node = 0;
	double m_phase = 0.0;
	double m_segment_phase = 0.0;
	double m_next_segment_time = 0.0;
	std::vector<GendynNode> m_nodes;
	std::mt19937 m_rand;
	float m_cur_dur = 0.0;
	float m_cur_y0 = 0.0;
	float m_cur_y1 = 0.0;
};

class GendynModule : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_NumSegments,
		PAR_TimeDistribution,
		PAR_TimeResetMode,
        PAR_TimePrimaryBarrierLow,
        PAR_TimePrimaryBarrierHigh,
        PAR_TimeSecondaryBarrierLow,
        PAR_TimeSecondaryBarrierHigh,
        PAR_TimeMean,
        PAR_TimeDeviation,
        PAR_AmpDistribution,
		PAR_AmpResetMode,
		PAR_AmpPrimaryBarrierLow,
        PAR_AmpPrimaryBarrierHigh,
        PAR_AmpSecondaryBarrierLow,
        PAR_AmpSecondaryBarrierHigh,
        PAR_AmpMean,
        PAR_AmpDeviation,
		PAR_PolyphonyVoices,
		PAR_CenterFrequency,
        LASTPAR
    };
	
    GendynModule();
    std::string getDebugMessage();
    void process(const ProcessArgs& args) override;
private:
    GendynOsc m_oscs[16];
	dsp::SchmittTrigger m_reset_trigger;
	dsp::ClockDivider m_divider;
};

class GendynWidget : public ModuleWidget
{
public:
    GendynWidget(GendynModule* m);
    void draw(const DrawArgs &args) override;
};
