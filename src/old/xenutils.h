#pragma once

#include <rack.hpp>
#include "plugin.hpp"
#include <functional>
#include <atomic>


class DecahexCVTransformer : public rack::Module
{
public:
    enum TransformTypes
    {
        Linear,
        Steps
    };
    enum Pars
    {
        ENUMS(INLOW,16),
        ENUMS(INHIGH,16),
        ENUMS(TRANSFORMTYPE,16),
        ENUMS(TRANSFORMPARA,16),
        ENUMS(TRANSFORMPARB,16),
        ENUMS(OUTOFFSET,16),
        ENUMS(OUTGAIN,16)
    };
    enum Inputs
    {
        ENUMS(INSIG,16)
    };
    enum Outputs
    {
        ENUMS(OUTSIG,16)
    };
    DecahexCVTransformer()
    {
        config(OUTGAIN_LAST+1,INSIG_LAST+1,OUTSIG_LAST+1);
        for (int i=0;i<INSIG_LAST+1;++i)
        {
            configParam(INLOW+i,-10.0f,10.0f,-10.0f);
            configParam(INHIGH+i,-10.0f,10.0f,10.0f);
            configParam(TRANSFORMTYPE+i,0.0f,4.0f,0.0f);
            configParam(TRANSFORMPARA+i,0.0f,1.0f,0.0f);
            configParam(TRANSFORMPARB+i,0.0f,1.0f,0.0f);
            configParam(OUTOFFSET+i,-10.0f,10.0f,0.0f);
            configParam(OUTGAIN+i,-2.0f,2.0f,1.0f);
        }
    }
    void process(const ProcessArgs& args) override
    {
        for (int i=0;i<INSIG_LAST+1;++i)
        {
            if (inputs[i].isConnected())
            {
                float v = rescale(inputs[i].getVoltage(),
                    params[INLOW+i].getValue(),params[INHIGH+i].getValue(),
                    0.0f,1.0f);
                float par_a = params[TRANSFORMPARA+i].getValue();
                //float par_b = params[TRANSFORMPARB+i].getValue();
                int ttype = params[TRANSFORMTYPE+i].getValue();
                if (ttype==Steps)
                {
                    int numsteps = 1+par_a*99;
                    v = round(v*(int)numsteps)/(int)numsteps;
                }
                v = (v-0.5f)*2.0f*params[OUTGAIN+i].getValue()*10.0f;
                v += params[OUTOFFSET+i].getValue();
                v = clamp(v,-10.0f,10.0f);
                outputs[i].setVoltage(v);
            }
            
        }
    }
};

class DecahexCVTransformerWidget : public ModuleWidget
{
public:
    DecahexCVTransformerWidget(DecahexCVTransformer*);
    void draw(const DrawArgs &args) override;  
};

class Delay
{
public:
    Delay(int delayTime_) : delayTime(delayTime_)
    {
        delayBuffer.resize(65536); // 65536 is the maximum delay size
        writeIndex = delayTime;
    }
    void reset(int newDelayTime) 
    { 
        std::fill(delayBuffer.begin(),delayBuffer.end(),0.0f); 
        writeIndex = newDelayTime;
        readIndex = 0;
    }
    float process(float insample)
    {
        float out = delayBuffer[readIndex];
        readIndex = (readIndex+1) % delayBuffer.size();
        delayBuffer[writeIndex]=insample;
        writeIndex = (writeIndex+1) % delayBuffer.size();
        return out;
    }
private:
    std::vector<float> delayBuffer;
    int readIndex = 0;
    int writeIndex = 0;
    int delayTime = 0;
};
