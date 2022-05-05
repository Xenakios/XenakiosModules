#pragma once

#include <vector>
//#include "plugin.hpp"
#include <Tunings.h>
#include "mischelpers.h"

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

template<typename ResultType>
inline std::vector<ResultType> semitonesFromScalaScale(Tunings::Scale& thescale,
    double startPitch,double endPitch)
{
    bool finished = false;
    std::vector<ResultType> voltScale;
    int sanity = 0;
    double volts = startPitch;
    voltScale.push_back(volts);
    double endvalue = endPitch;
    while (volts < endvalue)
    {
        double last = 0.0;
        for (auto& e : thescale.tones)
        {
            double cents = e.cents;
            double evolt = cents/100.0; 
            if (volts + evolt > endvalue)
            {
                finished = true;
                break;
            }
            voltScale.push_back(volts + evolt);
            last = evolt;
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
