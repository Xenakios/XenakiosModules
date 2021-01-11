#pragma once
#include <rack.hpp>

using namespace rack;
#include <fstream>
#include <functional>

// Declare the Plugin, defined in plugin.cpp
extern Plugin *pluginInstance;

const float g_pi = 3.14159265358979;

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

inline std::shared_ptr<rack::Font> getDefaultFont(int which)
{
	static std::map<int,std::shared_ptr<rack::Font>> s_fonts;
	if (s_fonts.count(which)==0)
	{
		std::shared_ptr<rack::Font> font;
		if (which == 0)
			font = APP->window->loadFont(asset::plugin(pluginInstance, "res/Nunito-Bold.ttf"));
		else
			font = APP->window->loadFont(asset::plugin(pluginInstance, "res/Nunito-Bold.ttf"));
		s_fonts[which] = font;
	}
		
	return s_fonts[which];
}

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

const int msnumtables = 8;
const int mstablesize = 1024;

class ModulationShaper
{
public:
    ModulationShaper()
    {
        float randvalues[1024];
        for (int i=0;i<1024;++i)
            randvalues[i]=random::normal()*0.1f;
        for (int i=0;i<mstablesize;++i)
        {
            float norm = 1.0/(mstablesize-1)*i;
            m_tables[0][i] = std::pow(norm,5.0f);
            m_tables[1][i] = std::pow(norm,2.0f);
            m_tables[2][i] = norm;
            m_tables[3][i] = 0.5-0.5*std::sin(3.141592653*(0.5+norm));
            m_tables[4][i] = 1.0f-std::pow(1.0f-norm,5.0f);
            float smoothrand = interpolateLinear(randvalues,norm*32.0f);
            m_tables[5][i] = clamp(norm+smoothrand,0.0,1.0f);
            smoothrand = interpolateLinear(randvalues,32.0f+norm*48.0f);
            m_tables[6][i] = clamp(norm+smoothrand,0.0,1.0f);
            m_tables[7][i] = std::round(norm*7)/7;
        }
        // fill guard point by repeating value
        for (int i=0;i<msnumtables;++i)
            m_tables[i][mstablesize] = m_tables[i][mstablesize-1];
        // fill guard table by repeating table
        for (int i=0;i<mstablesize;++i)
            m_tables[msnumtables][i]=m_tables[msnumtables-1][i];
    }
	float processNonMorph(int tableindex, float input)
	{
		return interpolateLinear(m_tables[tableindex],input*mstablesize);
	}
    float process(float morph, float input)
    {
        float z = morph*(msnumtables-1);
        int xindex0 = morph*(msnumtables-1);
        int xindex1 = xindex0+1;
        int yindex0 = input*(mstablesize-1);
        int yindex1 = yindex0+1;
        float x_a0 = m_tables[xindex0][yindex0];
        float x_a1 = m_tables[xindex0][yindex1];
        float x_b0 = m_tables[xindex1][yindex0];
        float x_b1 = m_tables[xindex1][yindex0];
        float xfrac = (input*mstablesize)-yindex0;
        float x_interp0 = x_a0+(x_a1-x_a0) * xfrac;
        float x_interp1 = x_b0+(x_b1-x_b0) * xfrac;
        float yfrac=z-(int)z;
        return x_interp0+(x_interp1-x_interp0) * yfrac;
        
    }
private:
    
    float m_tables[msnumtables+1][mstablesize+1];

};


// Declare each Model, defined in each module source file
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
