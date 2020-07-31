#include "plugin.hpp"
#include <random>
#include <atomic>

extern std::shared_ptr<Font> g_font;

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


class Quantizer
{
public:
    Quantizer()
    {
        std::mt19937 gen;
        std::uniform_real_distribution<float> dist(-10.0,10.0f);
        //for (int i=0;i<7;++i)
        //    voltages.push_back(dist(gen));
        //std::sort(voltages.begin(),voltages.end());
        voltages = {-5.0f,5.0f};
    }
    float process(float x, float strength)
    {
        return quantize_to_grid(x,voltages,strength);

        auto it = std::lower_bound(voltages.begin(),voltages.end(),x);
        //if (it == voltages.end())
        //    return x;
        --it;
        float q0 = *it;
        ++it;
        if (it == voltages.end())
            --it;
        float q1 = *it;
        float d0 = fabs(q0-x);
        float d1 = fabs(q1-x);
        if (d0<d1)
            return q0;
        return q1;
    }
    std::vector<float> voltages;
private:
    
};

class XQuantModule : public rack::Module
{
public:
    enum InputPorts
    {
        FIRSTINPUT = 0,
        LASTINPUT = 7
    };
    enum OutputPorts
    {
        FIRSTOUTPUT = 0,
        LASTOUTPUT = FIRSTOUTPUT+7
    };
    std::vector<float> heldOutputs;
    std::atomic<bool> shouldUpdate{false};
    std::vector<float> swapVector;
    int whichQToUpdate = -1;
    dsp::ClockDivider divider;
    XQuantModule()
    {
        divider.setDivision(16);
        heldOutputs.resize(8);
        config(1,8,8,0);
        configParam(0,0.0f,1.0f,0.05f,"Foopar");
    }
    void updateQuantizerValues(int index, std::vector<float> values)
    {
        std::sort(values.begin(),values.end());
        swapVector = values;
        whichQToUpdate = index;
        shouldUpdate = true;
    }
    void process(const ProcessArgs& args) override
    {
        if (divider.process())
        {
            if (shouldUpdate)
            {
                shouldUpdate = false;
                std::swap(quantizers[whichQToUpdate].voltages,swapVector);
                whichQToUpdate = -1;
            }
            float strength = params[0].getValue();
            for (int i=0;i<8;++i)
            {
                if (outputs[i].isConnected())
                    heldOutputs[i] = quantizers[i].process(inputs[i].getVoltage(),strength);
            }
        }
        for (int i=0;i<8;++i)
        {
            outputs[i].setVoltage(heldOutputs[i]);
        }
        
    }
    Quantizer quantizers[8];
};

class QuantizeValuesWidget : public TransparentWidget
{
public:
    XQuantModule* qmod = nullptr;
    int which_ = 0;
    bool& dirty;
    int draggedValue_ = -1;
    int startXcor = 0;
    float startDragVal = 0.0;
    QuantizeValuesWidget(XQuantModule* m,int which, bool& dir) 
        : qmod(m), which_(which),dirty(dir)
    {
        dirty = true;
    }
    int findQuantizeIndex(float xcor, float ycor)
    {
        auto& v = qmod->quantizers[which_].voltages;
        for (int i=0;i<v.size();++i)
        {
            Rect r(rescale(v[i],-5.0f,5.0f,0.0,box.size.x)-10.0f,0,20.0f,
                box.size.y);
            if (r.contains({xcor,ycor}))
            {
                return i;
            }
        }
        return -1;
    }
    void onDragMove(const event::DragMove& e) override
    {
        auto v = qmod->quantizers[which_].voltages;
        e.stopPropagating();
        
        float delta = e.mouseDelta.x*0.1;
        
        int newXcor = startXcor+e.mouseDelta.x;
        startXcor = newXcor;
        float val = rescale(newXcor,0.0,box.size.x,-5.0f,5.0f);
        val = startDragVal+e.mouseDelta.x*(10.0/box.size.x);
        val = clamp(val,-5.0f,5.0f);
        v[draggedValue_]=val;
        startDragVal = val;
        dirty = true;
        qmod->updateQuantizerValues(which_,v);
        //float newv = rescale(e.pos.x,0,box.size.x,-10.0f,10.0f);
    }
    void onButton(const event::Button& e) override
    {
        
        if (e.action == GLFW_RELEASE)
        {
            draggedValue_ = -1;
            dirty = true;
            return;
        }
        
        int index = findQuantizeIndex(e.pos.x,e.pos.y);
        auto v = qmod->quantizers[which_].voltages;
        if (index>=0)
        {
            
            e.consume(this);
            draggedValue_ = index;
            startXcor = e.pos.x;
            startDragVal = v[index];
            return;
        }
<<<<<<< HEAD
=======
        ++buttonCounter;
>>>>>>> 46073f4ba61c5ceff3598082061d5b51de964c32
        if (index == -1)
        {
            
            float newv = rescale(e.pos.x,0,box.size.x,-5.0f,5.0f);
            v.push_back(newv);
        }
        if (e.mods == GLFW_MOD_SHIFT)
        {
            
            if (index>=0 && v.size()>1)
            {
                v.erase(v.begin()+index);
            }
        }
        qmod->updateQuantizerValues(which_,v);
        dirty = true;
    }
    int buttonCounter = 0;
    void draw(const DrawArgs &args) override
    {
        if (!qmod)
            return;
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGB(0,128,0));
        nvgRect(args.vg,0,0,box.size.x,box.size.y);
        nvgFill(args.vg);
        nvgStrokeColor(args.vg,nvgRGB(255,255,255));
        auto& qvals = qmod->quantizers[which_].voltages;
        int numqvals = qvals.size();
        for (int i=0;i<numqvals;++i)
        {
            float xcor = rescale(qvals[i],-5.0f,5.0f,0.0,box.size.x);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,box.size.y);
            nvgStroke(args.vg);
        }
        nvgFontSize(args.vg, 15);
        nvgFontFaceId(args.vg, g_font->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        char txt[100];
        sprintf(txt,"click %d",buttonCounter);
        nvgText(args.vg, 3 , 10, txt, NULL);
        nvgRestore(args.vg);
    }
};

class XQuantWidget : public ModuleWidget
{
public:
    bool dummy = false;
    XQuantWidget(XQuantModule* m)
    {
        if (!g_font)
        	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
        setModule(m);
        box.size.x = 400;
        for (int i=0;i<8;++i)
        {
            addInput(createInputCentered<PJ301MPort>(Vec(30, 30+i*30), m, XQuantModule::FIRSTINPUT+i));
#ifdef USEFBFORQW
            auto fbWidget = new FramebufferWidget;
		    fbWidget->box.pos = Vec(50.0f,17.5+30.0f*i);
            fbWidget->box.size = Vec(300.0,25);
		    addChild(fbWidget);
            QuantizeValuesWidget* qw = 
                new QuantizeValuesWidget(m,i,fbWidget->dirty);
            //qw->box.pos = Vec(50.0f,15.0+30.0f*i);
            qw->box.size = Vec(300.0,25);
            fbWidget->addChild(qw);
#else
            QuantizeValuesWidget* qw = 
                new QuantizeValuesWidget(m,i,dummy);
            qw->box.pos = Vec(50.0f,15.0+30.0f*i);
            qw->box.size = Vec(300.0,25);
            addChild(qw);
#endif
            addOutput(createOutputCentered<PJ301MPort>(Vec(370, 30+i*30), m, XQuantModule::FIRSTOUTPUT+i));
        }
        addParam(createParam<RoundLargeBlackKnob>(Vec(38, 270), module, 0));
    }
    void draw(const DrawArgs &args)
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x40, 0x40, 0x40, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);

        nvgFontSize(args.vg, 15);
        nvgFontFaceId(args.vg, g_font->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        nvgText(args.vg, 3 , 10, "XQuantizer", NULL);
        nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }

};

Model* modelXQuantizer = createModel<XQuantModule, XQuantWidget>("XQuantizer");