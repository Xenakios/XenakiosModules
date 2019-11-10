#pragma once

#include <rack.hpp>
#include "plugin.hpp"

const int NUMSNAPSHOTS = 32;

class KeyFramerModule : public rack::Module
{
public:
    KeyFramerModule();
    void process(const ProcessArgs& args) override;
    void updateSnapshot(int index)
    {
        if (index>=0 && index < m_maxsnapshots)
        {
            for (int i=0;i<8;++i)
            {
                m_scenes[index][i] = params[i+1].getValue();
            }
        }
    }
    void recallSnapshot(int index)
    {
        if (index>=0 && index < m_maxsnapshots)
        {
            params[0].setValue(rescale(index,0,m_maxsnapshots-1,0.0f,1.0));
            for (int i=0;i<8;++i)
            {
                params[i+1].setValue(m_scenes[index][i]);
            }
        }
    }
    int m_maxsnapshots = 32;
    float m_cur_morph = 0.0f;
private:
    float m_interpolated[8];
    float m_scenes[NUMSNAPSHOTS][8];
};

class KeyFramerWidget : public ModuleWidget
{
public:
    KeyFramerWidget(KeyFramerModule* m);
    void draw(const DrawArgs &args) override;
};



