#pragma once

#include <rack.hpp>
#include "plugin.hpp"
#include <functional>
#include <atomic>
#include <random>

template<typename T>
inline T wrap_value(const T& minval, const T& val, const T& maxval)
{
	T temp = val;
	while (temp<minval || temp>maxval)
	{
		if (temp < minval)
			temp = maxval - (minval - temp);
		if (temp > maxval)
			temp = minval - (maxval - temp);
	}
	return temp;
}

template<typename T>
inline T reflect_value(const T& minval, const T& val, const T& maxval)
{
	T temp = val;
	while (temp<minval || temp>maxval)
	{
		if (temp < minval)
			temp = minval + (minval - temp);
		if (temp > maxval)
			temp = maxval + (maxval - temp);
	}
	return temp;
}


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
	GendynOsc(int seed) : m_rand(seed)
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
	}
	void process(float* buf, int nframes)
	{
		for (int i = 0; i < nframes; ++i)
		{
			float t1 = m_cur_dur;
			
			float y0 = m_cur_y0;
			float y1 = m_cur_y1;
			float s = y0 + (y1 - y0) / t1 * m_phase;
			buf[i] = s;
			m_phase += 1.0;
			if (m_phase >= t1)
			{
				++m_cur_node;
				
				if (m_cur_node < m_num_segs - 1)
				{
					m_cur_dur = m_nodes[m_cur_node].m_x_sec;
					m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
					m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
				}
				if (m_cur_node == m_num_segs - 1)
				{
					m_cur_dur = m_nodes[m_cur_node].m_x_sec;
					m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
					updateTable();
					m_cur_y1 = m_nodes.front().m_y_sec;
				}
				if (m_cur_node == m_num_segs)
				{
					
					m_cur_node = 0;
					m_cur_dur = m_nodes[m_cur_node].m_x_sec;
					m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
					m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
				}
				m_phase = 0.0;
			}
		}
	}
	void updateTable()
	{
		std::normal_distribution<float> timedist(m_time_mean, m_time_dev);
		std::normal_distribution<float> ampdist(m_amp_mean, m_amp_dev);
		for (int i = 0; i < m_num_segs; ++i)
		{
			float x_p = m_nodes[i].m_x_prim;
			x_p += timedist(m_rand);
			x_p = reflect_value(m_time_primary_low_barrier, x_p,m_time_primary_high_barrier);
			float x_s = m_nodes[i].m_x_sec;
			x_s += x_p;
			x_s = reflect_value(m_time_secondary_low_barrier, x_s, m_time_secondary_high_barrier);
			m_nodes[i].m_x_prim = x_p;
			m_nodes[i].m_x_sec = x_s;
			float y_p = m_nodes[i].m_y_prim;
			y_p += ampdist(m_rand);
			y_p = clamp(y_p,m_amp_primary_low_barrier, m_amp_primary_high_barrier);
			float y_s = m_nodes[i].m_y_sec;
			y_s += y_p;
			y_s = clamp(y_s,m_amp_secondary_low_barrier, m_amp_secondary_high_barrier);
			m_nodes[i].m_y_prim = y_p;
			m_nodes[i].m_y_sec = y_s;
		}
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
private:
	int m_cur_node = 0;
	double m_phase = 0.0;
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
        PAR_TimePrimaryBarrierLow,
        PAR_TimePrimaryBarrierHigh,
        PAR_TimeSecondaryBarrierLow,
        PAR_TimeSecondaryBarrierHigh,
        PAR_TimeMean,
        PAR_TimeDeviation,
        PAR_AmpPrimaryBarrierLow,
        PAR_AmpPrimaryBarrierHigh,
        PAR_AmpSecondaryBarrierLow,
        PAR_AmpSecondaryBarrierHigh,
        PAR_AmpMean,
        PAR_AmpDeviation,
        LASTPAR
    };
    GendynModule();
    
    void process(const ProcessArgs& args) override;
private:
    GendynOsc m_osc{0};
};

class GendynWidget : public ModuleWidget
{
public:
    GendynWidget(GendynModule* m);
    void draw(const DrawArgs &args) override;
};
