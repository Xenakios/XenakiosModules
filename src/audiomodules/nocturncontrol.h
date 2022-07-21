#pragma once

#include <vector>
#include <atomic>

class NocturnControlBase
{
public:
    virtual ~NocturnControlBase() {}
    static void MidiCallback( double /*timeStamp*/, std::vector<unsigned char> *message, void *userData )
    {
        if (!message)
            return;
        auto& msg = *message;
        if (msg.size()!=3)
            return;
        NocturnControlBase* ncb = (NocturnControlBase*)userData;
        if (msg[0] >= 176)
        {
            if (msg[1] == 112) // button 1
            {
                if (msg[2]>0)
                    ncb->m_shiftstate = 1;
                else ncb->m_shiftstate = 0;
            }
            if (msg[1] == 122) // page - button
            {
                if (msg[2]>0)
                    ncb->onStepPage(-1);
            }
            if (msg[1] == 123) // page + button
            {
                if (msg[2]>0)
                    ncb->onStepPage(1);
            }
            if (msg[1] >= 113 && msg[1] <= 119) // buttons 2-8
            {
                if (msg[2]>0)
                    ncb->onButton(msg[1]-112,true);
                else
                    ncb->onButton(msg[1]-112,false);
            }
            if (msg[1] == 120 && msg[2]>0) // "learn" button
            {
                ncb->onLearnButton();
            }
            if (msg[1] == 72 && ncb->m_crossfaderstate == 0)
            {
                ncb->m_crossfadervalues[0] = msg[2];
                ncb->m_crossfaderstate = 1;
            }
            if (msg[1] == 73 && ncb->m_crossfaderstate == 1)
            {
                ncb->m_crossfadervalues[1] = msg[2];
                ncb->m_crossfaderstate = 2;
            }
            if (ncb->m_crossfaderstate == 2)
            {
                ncb->m_crossfaderstate = 0;
                int thevalue = ncb->m_crossfadervalues[0]*128+ncb->m_crossfadervalues[1];
                float bigfadernorm = 1.0f/16384*thevalue;
                ncb->onCrossFader(bigfadernorm);
            }
            int delta = 0;
            if (msg[2]>=64)
            {
                if (msg[2]==127)
                    delta = -1;
                else 
                    delta = -2;
            }
            if (msg[2]<64)
            {
                if (msg[2]==1)
                    delta = 1;
                else delta = 2;
            }
            if (delta !=0 && msg[1] >= 64 && msg[1] <= 71)
            {
                ncb->onEncoder(msg[1]-64,delta);
            }
        }
    }    
    virtual void onEncoder(int idx, int step) = 0;
    virtual void onCrossFader(float val) = 0;
    virtual void onStepPage(int step) = 0;
    virtual void onButton(int idx, bool down) = 0;
    virtual void onLearnButton() = 0;
    int getShiftState() { return m_shiftstate; }
protected:
    int m_crossfaderstate = 0;
    int m_crossfadervalues[2] = {0,0};
    std::atomic<int> m_shiftstate{0};
};
