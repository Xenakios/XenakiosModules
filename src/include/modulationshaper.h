#pragma once

//#include <rack.hpp>

#include <random.hpp>
#include <math.hpp>

using namespace rack;
using namespace rack::math;

inline float adjustable_triangle(float in, float peakpos)
{
    in = fmod(in,1.0f);
    if (in<peakpos)
        return rescale(in,0.0f,peakpos,0.0f,1.0f);
    return rescale(in,peakpos,1.0f,1.0f,0.0f);
}

inline float squarewave(float in)
{
    in = fmod(in,1.0f);
    if (in<0.5f)
        return 0.0;
    return 1.0f;
}

inline float easeOutBounce(float x)
{
    const float n1 = 7.5625;
    const float d1 = 2.75;

    if (x < 1 / d1) {
        return n1 * x * x;
    } else if (x < 2 / d1) {
        return n1 * (x -= 1.5 / d1) * x + 0.75;
    } else if (x < 2.5 / d1) {
        return n1 * (x -= 2.25 / d1) * x + 0.9375;
    } else {
        return n1 * (x -= 2.625 / d1) * x + 0.984375;
    }
}

inline float easeInBounce(float x) 
{
    return 1.0 - easeOutBounce(1.0 - x);
}

inline float easeInElastic(float x)
{
    const float c4 = (2 * 3.141592653) / 3.0;
    if (x <= 0.0f)
        return 0.0f;
    if (x >= 1.0f)
        return 1.0f;
    return -pow(2, 10 * x - 10) * sin((x * 10 - 10.75) * c4);
}

const int msnumtables = 16;
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
            m_tables[6][i] = clamp(norm+smoothrand*5.0f,0.0,1.0f);
            m_tables[7][i] = std::round(norm*7)/7;
			m_tables[8][i] = std::round(norm*10)/10;
			int indexx = 128.0+19.0*norm;
			m_tables[9][i] = clamp(norm+randvalues[indexx]*2.0f,0.0f,1.0f);
			float y0 = norm;
			float y1 = rescale(norm,0.0f,1.0f,0.5f,1.0f);
			float y2 = adjustable_triangle(norm*16.0,0.5f);
			float y3 = rescale(y2,0.0f,1.0f,y0,y1);
			m_tables[10][i] = y3;
			y0 = rescale(norm,0.0f,1.0f,0.0f,0.5f);
			y1 = rescale(norm,0.0f,1.0f,0.5f,1.0f);
			y2 = adjustable_triangle(norm*12.0,0.5f);
			y3 = rescale(y2,0.0f,1.0f,y0,y1);
			m_tables[11][i] = y3;
            m_tables[12][i] = squarewave(norm*5.0);
            m_tables[13][i] = easeOutBounce(norm);
            m_tables[14][i] = easeInBounce(norm);
            m_tables[15][i] = easeInElastic(norm);
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
