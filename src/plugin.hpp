#pragma once
#include <rack.hpp>

using namespace rack;
#include <fstream>
#include <functional>
#include "grain_engine/dr_wav.h"
// Declare the Plugin, defined in plugin.cpp
extern Plugin *pluginInstance;

const float g_pi = 3.14159265358979;

template<typename T, size_t Sz>
inline std::array<T, Sz> makeArray()
{
	std::array<T,Sz> result;
	std::fill(result.begin(),result.end(),T{});
	return result;
}

inline float customlog(float base, float x)
{
	return std::log(x)/std::log(base);
}

class OnePoleFilter
{
public:
    OnePoleFilter() {}
    void setAmount(float x)
    {
        a = x;
        b = 1.0f-a;
    }
    inline __attribute__((always_inline)) float process(float x)
    {
        float temp = (x * b) + (z * a);
        z = temp;
        return temp;
    }
private:
    float z = 0.0f;
    float a = 0.99f;
    float b = 1.0f-a;

};

inline std::pair<int, int> parseFractional(std::string& str)
{
	int pos = str.find('/');
	auto first = str.substr(0, pos);
	auto second = str.substr(pos + 1);
	return { std::stoi(first),std::stoi(second) };
}

inline std::vector<double> parse_scala(std::vector<std::string>& input,
    bool outputSemitones=false)
{
	std::vector<double> result;
	bool desc_found = false;
	int num_notes_found = 0;
	result.push_back(0.0);
	for (auto& e : input)
	{
		if (e[0] == '!')
			continue;
		if (num_notes_found > 0)
		{
			if (e.find('.')!=std::string::npos)
			{
				double freq = atof(e.c_str());
				if (freq > 0.0)
				{
                    if (!outputSemitones)
					    result.push_back(1.0/1200.0*freq);
                    else result.push_back(freq/100.0);
				}
			}
			else if (e.find("/")!=std::string::npos)
			{
				auto fract = parseFractional(e);
				//std::cout << (double)fract.first/(double)fract.second << " (from fractional)\n";
				double f0 = fract.first;
				double f1 = fract.second;
				if (!outputSemitones)
				    result.push_back(customlog(2.0f,f0/f1));
                else result.push_back(customlog(2.0f,f0/f1)*12.0f);
			}
			else
			{
				try
				{
					int whole = std::stoi(e);
                    if (!outputSemitones)
					    result.push_back(customlog(2.0f,2.0/whole));
                    else result.push_back(customlog(2.0f,2.0/whole)*12.0f);
				}
				catch (std::exception& ex)
				{
					//std::cout << ex.what() << "\n";
				}
				
			}
		}
		if (e[0] != '!' && desc_found == true && num_notes_found==0)
		{
			//std::cout << e << "\n";
			int notes = atoi(e.c_str());
			if (notes < 1)
			{
				std::cout << "invalid num notes " << notes << "\n";
				break;
			}
			std::cout << "num notes in scale " << notes << "\n";
			num_notes_found = notes;
		}
		if (desc_found == false)
		{
			std::cout << "desc : " << e << "\n";
			desc_found = true;
		}
		
	}
	return result;
}

inline std::vector<float> loadScala(std::string path, 
	bool outputSemitones=false,float minpitch=0.0f,float maxpitch=0.0f)
{
    std::fstream f{ path };
	if (f.is_open())
	{
		std::vector<std::string> lines;
		char buf[4096];
		while (f.eof() == false)
		{
			f.getline(buf, 4096);
			lines.push_back(buf);
		}
		if (lines.size()==0)
		{
			return std::vector<float>();
		}
		auto result = parse_scala(lines,outputSemitones);
		std::sort(result.begin(),result.end());
        float volts = -5.0f;
        float endvalue = 5.0f;
        if (outputSemitones)
        {
            volts = minpitch;
            endvalue = maxpitch;
        }
		bool finished = false;
		std::vector<float> voltScale;
		int sanity = 0;
        while (volts < endvalue)
		{
			float last = 0.0f;
			for (auto& e : result)
			{
				if (volts + e > endvalue)
				{
					finished = true;
					break;
				}
				
				//std::cout << e << "\t\t" << volts+e << "\n";
				voltScale.push_back(volts + e);
				last = e;
			}
			volts += last;
			if (finished)
				break;
            ++sanity;
            if (sanity>1000)
                break;
		}
		voltScale.erase(std::unique(voltScale.begin(), voltScale.end()), voltScale.end());
		return voltScale;
	}
	else std::cout << "could not open file\n";
    return {};
}



template<typename T>
inline double grid_value(const T& ge)
{
    return ge;
}


#define VAL_QUAN_NORILO

template<typename T,typename Grid>
inline double quantize_to_grid(T x, const Grid& g, double amount=1.0)
{
    auto t1=std::lower_bound(std::begin(g),std::end(g),x);
    if (t1!=std::end(g))
    {
        /*
        auto t0=t1-1;
        if (t0<std::begin(g))
            t0=std::begin(g);
        */
        auto t0=std::begin(g);
        if (t1>std::begin(g))
            t0=t1-1;
#ifndef VAL_QUAN_NORILO
        const T half_diff=(*t1-*t0)/2;
        const T mid_point=*t0+half_diff;
        if (x<mid_point)
        {
            const T diff=*t0-x;
            return x+diff*amount;
        } else
        {
            const T diff=*t1-x;
            return x+diff*amount;
        }
#else
        const double gridvalue = fabs(grid_value(*t0) - grid_value(x)) < fabs(grid_value(*t1) - grid_value(x)) ? grid_value(*t0) : grid_value(*t1);
        return x + amount * (gridvalue - x);
#endif
    }
    auto last = std::end(g)-1;
    const double diff=grid_value(*(last))-grid_value(x);
    return x+diff*amount;
}

template <typename TLightBase = RedLight>
struct LEDLightSliderFixed : LEDLightSlider<TLightBase> {
	LEDLightSliderFixed() {
		this->setHandleSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LEDSliderHandle.svg")));
	}
};

std::shared_ptr<rack::Font> getDefaultFont(int which);

inline float soft_clip(float x)
{
    if (x<-1.0f)
        return -2.0f/3.0f;
    if (x>1.0f)
        return 2.0f/3.0f;
    return x-(std::pow(x,3.0f)/3.0f);
}


template<typename T>
inline T wrap_value(const T& minval, const T& val, const T& maxval)
{
	if (minval==maxval)
		return minval;
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
	if (minval==maxval)
		return minval;
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

struct LambdaItem : rack::MenuItem
{
	std::function<void(void)> ActionFunc;
	void onAction(const event::Action &e) override
	{
		if (ActionFunc)
			ActionFunc();
	}
};

template <class Func>
inline rack::MenuItem * createMenuItem(Func f, std::string text, std::string rightText = "") {
	LambdaItem* o = new LambdaItem;
	o->text = text;
	o->rightText = rightText;
	o->ActionFunc = f;
	return o;
}

class DrWavBuffer
{
public:
    DrWavBuffer() {}
    DrWavBuffer(float* src, drwav_uint64 numFrames)
    {
        m_buf = src;
        m_sz = numFrames;
    }
    ~DrWavBuffer()
    {
        if (m_buf)
            drwav_free(m_buf, nullptr);
    }
    DrWavBuffer(const DrWavBuffer&) = delete;
    DrWavBuffer& operator=(const DrWavBuffer&) = delete;
    DrWavBuffer(DrWavBuffer&& other)
    {
        m_buf = other.m_buf;
        m_sz = other.m_sz;
        other.m_buf = nullptr;
        other.m_sz = 0;
    }
    DrWavBuffer& operator=(DrWavBuffer&& other)
    {
        std::swap(m_buf,other.m_buf);
        std::swap(m_sz,other.m_sz);
        return *this;
    }
    float* data() { return m_buf; }
    drwav_uint64 size() { return m_sz; }
private:
    float* m_buf = nullptr;
    drwav_uint64 m_sz = 0;
};



// Declare each Model, defined in each module source file

extern Model* modelGendynOSC;
extern Model* modelPolyClock;
extern Model* modelRandomClock;
extern Model *modelRandom;
extern Model *modelXQuantizer;
extern Model* modelXPSynth;
extern Model* modelInharmonics;
extern Model* modelXStochastic;
extern Model* modelXImageSynth;
extern Model* modelXLOFI;
extern Model* modelXMultiMod;
extern Model* modelXGranular;
extern Model* modelXEnvelope;
extern Model* modelXCVShaper;
extern Model* modelXSampler;
extern Model* modelXScaleOscillator;
extern Model* modelXRandom;