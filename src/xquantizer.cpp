#include "plugin.hpp"
#include <random>
#include <atomic>

const int NUM_QUANTIZERS = 8;

extern std::shared_ptr<Font> g_font;

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
        voltages = {-5.0f,0.0f,5.0f};
    }
    void sortVoltages()
    {
        std::sort(voltages.begin(),voltages.end());
    }
    void rotateVoltages(float shift)
    {
        for (int i=0;i<voltages.size();++i)
        {
            voltages[i] = wrap_value(-5.0f,voltages[i]+shift,5.0f);
        }
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
        FIRST_ROT_CV_INPUT = 8,
        NUM_INPUTS = 16
    };
    enum OutputPorts
    {
        FIRSTQUANOUTPUT = 0,
        FIRSTGATEOUTPUT = 8,
        NUMOUTPUTS = 16
    };
    enum Parameters
    {
        ENUMS(AMOUNT_PARAM, 8),
        ENUMS(ROTATE_PARAM, 8)
    };
    std::vector<float> heldOutputs;
    std::vector<float> triggerOutputs;
    std::atomic<bool> shouldUpdate{false};
    std::vector<float> swapVector;
    int whichQToUpdate = -1;
    Quantizer quantizers[NUM_QUANTIZERS];
    dsp::ClockDivider divider;
    dsp::PulseGenerator triggerPulses[8];
    float lastRotates[8];
    XQuantModule()
    {
        divider.setDivision(128);
        heldOutputs.resize(NUM_QUANTIZERS);
        triggerOutputs.resize(NUM_QUANTIZERS,0.0f);
        config(16,NUM_INPUTS,NUMOUTPUTS);
        for (int i=0;i<8;++i)
        {
            lastRotates[i] = 0.0f;
            configParam(AMOUNT_PARAM+i,0.0f,1.0f,1.00f,"Amount "+std::to_string(i));    
            configParam(ROTATE_PARAM+i,-5.0f,5.0f,0.00f,"Rotate "+std::to_string(i));    
        }
        
    }
    void updateQuantizerValues(int index, std::vector<float> values, bool dosort)
    {
        if (dosort)
            std::sort(values.begin(),values.end());
        swapVector = values;
        whichQToUpdate = index;
        shouldUpdate = true;
    }
    void updateSingleQuantizerValue(int quantizerindex, int index, float value)
    {
        quantizers[quantizerindex].voltages[index] = value;
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
            
            for (int i=0;i<8;++i)
            {
                float strength = params[AMOUNT_PARAM+i].getValue();
                float rot = params[ROTATE_PARAM+i].getValue();
                rot += inputs[FIRST_ROT_CV_INPUT+i].getVoltage();
                rot = clamp(rot,-5.0f,5.0f);
                if (lastRotates[i]!=rot)
                {
                    float delta = rot-lastRotates[i];
                    quantizers[i].rotateVoltages(delta);
                    lastRotates[i] = rot;
                }
                if (outputs[i].isConnected())
                {
                    float quanval = quantizers[i].process(inputs[i].getVoltage(),strength);
                    bool outchanged = false;
                    if (fabs(heldOutputs[i]-quanval)>0.04666)
                        outchanged = true;
                    heldOutputs[i] = quanval;
                    if (outputs[FIRSTGATEOUTPUT+i].isConnected() && outchanged)
                    {
                        if (triggerPulses[i].remaining>0.0)
                            triggerPulses[i].reset();
                        triggerPulses[i].trigger(0.002);
                    }
                }
            }
        }
        for (int i=0;i<8;++i)
        {
            if (outputs[i].isConnected())
            {
                outputs[i].setVoltage(heldOutputs[i]);
                float triggerOut = triggerPulses[i].process(args.sampleTime);
                outputs[FIRSTGATEOUTPUT+i].setVoltage(triggerOut*5.0f);
            }
            
        }
        
    }
    json_t* dataToJson() override
    {
        json_t* resultJ = json_object();
        json_t* arrayJ = json_array();
        for (int i=0;i<(int)NUM_QUANTIZERS;++i)
        {
            json_t* array2J = json_array();
            for (int j=0;j<quantizers[i].voltages.size();++j)
            {
                float v = quantizers[i].voltages[j];
                json_array_append(array2J,json_real(v));
            }
            json_array_append(arrayJ,array2J);
        }
        json_object_set(resultJ,"quantizers_v1",arrayJ);
        return resultJ;
    }
    void dataFromJson(json_t* root) override
    {
        json_t* arrayJ = json_object_get(root,"quantizers_v1");
        if (arrayJ)
        {
            for (int i=0;i<NUM_QUANTIZERS;++i)
            {
                json_t* array2J = json_array_get(arrayJ,i);
                if (array2J)
                {
                    quantizers[i].voltages.clear();
                    for (int j=0;j<json_array_size(array2J);++j)
                    {
                        float v = json_number_value(json_array_get(array2J,j));
                        quantizers[i].voltages.push_back(v);
                    }
                    quantizers[i].sortVoltages();
                }
            }
        }
    }
};

struct RotateSlider : ui::Slider {
					struct RotateQuantity : Quantity {
                        XQuantModule* module = nullptr;
                        int whichquant = 0;
                        float slidValue = 0.0f;
                        std::vector<float> originalValues;
						void setValue(float value) override {
							value = clamp(value,-5.0f,5.0f);
                            auto v = module->quantizers[whichquant].voltages;
                            for (int i=0;i<v.size();++i)
                            {
                                v[i] = wrap_value(-5.0f,originalValues[i]+value,5.0f);
                            }
                            module->updateQuantizerValues(whichquant,v,true);
                            slidValue = value;
						}
						float getValue() override {
							return slidValue;
						}
						float getDefaultValue() override {
							return 0.0f;
						}
						float getDisplayValue() override {
							return getValue();
						}
						void setDisplayValue(float displayValue) override {
							setValue(displayValue);
						}
						std::string getLabel() override {
							return "Rotate";
						}
						std::string getUnit() override {
							return "";
						}
						int getDisplayPrecision() override {
							return 3;
						}
						float getMaxValue() override {
							return 5.0f;
						}
						float getMinValue() override {
							return -5.0f;
						}
					};
                    XQuantModule* module = nullptr;
                    int whichQuant = -1;
					RotateSlider(XQuantModule* mod, int which) {
						box.size.x = 180.0f;
						RotateQuantity* q = new RotateQuantity;
                        q->module = mod;
                        q->originalValues = mod->quantizers[which].voltages;
                        q->whichquant = which;
                        quantity = q;
					}
					~RotateSlider() {
						delete quantity;
					}
				};

class QuantizeValuesWidget : public TransparentWidget
{
public:
    XQuantModule* qmod = nullptr;
    int which_ = 0;
    bool& dirty;
    int draggedValue_ = -1;
    bool quantizeDrag = false;
    float initX = 0.0f;
    float dragX = 0.0f;
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
            Rect r(rescale(v[i],-5.0f,5.0f,0.0,box.size.x)-2.0f,0,4.0f,
                box.size.y);
            if (r.contains({xcor,ycor}))
            {
                return i;
            }
        }
        return -1;
    }
    void onDragStart(const event::DragStart& e) override
    {
        dragX = APP->scene->rack->mousePos.x;
    }
    void onDragEnd(const event::DragEnd &e) override
    {

    }
    int mousemod = 0;
    float clampValue(const std::vector<float>& vec, int index, float input, float minval, float maxval)
    {
        if (index == 0)
            return clamp(input,minval,vec[1]-0.01);
        if (index == vec.size()-1)
            return clamp(input,vec[index-1]+0.01,maxval);
        int leftIndex = index - 1;
        int rightIndex = index + 1;
        return clamp(input,vec[leftIndex]+0.01,vec[rightIndex]-0.01);
        
    }
    void onDragMove(const event::DragMove& e) override
    {
        if (draggedValue_==-1)
            return;
        const auto& v = qmod->quantizers[which_].voltages;
        float newDragX = APP->scene->rack->mousePos.x;
        float newPos = initX+(newDragX-dragX);
        float val = rescale(newPos,0.0f,box.size.x,-5.0,5.0);
        if (quantizeDrag)
        {
            int temp = val * 6.0f;
            val = temp / 6.0f;
        }
        val = clampValue(v,draggedValue_,val,-5.0f,5.0f);
        qmod->updateSingleQuantizerValue(which_,draggedValue_,val);
        dirty = true;
        
        //float newv = rescale(e.pos.x,0,box.size.x,-10.0f,10.0f);
    }
    
    void onButton(const event::Button& e) override
    {
        struct ResetMenuItem : MenuItem
		{
			QuantizeValuesWidget* w = nullptr;
            void onAction(const event::Action &e) override
			{
				std::vector<float> v{-5.0f,0.0f,5.0f};
                w->qmod->updateQuantizerValues(w->which_,v,false);
			}
		};
        
        struct OctavesMenuItem : MenuItem
		{
			QuantizeValuesWidget* w = nullptr;
            int scaleType = 0;
            void onAction(const event::Action &e) override
			{
				std::vector<float> v;
                if (scaleType == 0)
                {
                    for (int i=0;i<11;++i)
                        v.push_back(-5.0+i*1.0f);
                } else if (scaleType == 1)
                {
                    std::vector<int> major{0,2,4,5,7,9,11};
                    for (int i=0;i<11;++i)
                    {
                        for (int j=0;j<major.size();++j)
                        {
                            float oct = -5.0+i*1.0f;
                            float pitch = 1.0/12*major[j];
                            if (oct+pitch<=5.0f)
                                v.push_back(oct+pitch);
                        }
                        
                    }
                }
                w->qmod->updateQuantizerValues(w->which_,v,false);
			}
		};

        mousemod = e.mods;
        if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS)
        {
            ui::Menu *menu = createMenu();
            MenuLabel *mastSetLabel = new MenuLabel();
			mastSetLabel->text = "Quantizer right click menu";
			menu->addChild(mastSetLabel);
            
            ResetMenuItem *resetItem = createMenuItem<ResetMenuItem>("Reset");
			resetItem->w = this;
			menu->addChild(resetItem);
            
            OctavesMenuItem* octaveItem = createMenuItem<OctavesMenuItem>("Set to octaves");
			octaveItem->w = this;
			menu->addChild(octaveItem);

            octaveItem = createMenuItem<OctavesMenuItem>("Set to major scale");
			octaveItem->w = this;
            octaveItem->scaleType = 1;
			menu->addChild(octaveItem);

            menu->addChild(new RotateSlider(qmod,which_));

            e.consume(this);
            return;
        }
        if (e.action == GLFW_RELEASE)
        {
            draggedValue_ = -1;
            dirty = true;
            return;
        }
        
        int index = findQuantizeIndex(e.pos.x,e.pos.y);
        auto v = qmod->quantizers[which_].voltages;
        if (index>=0 && !(e.mods & GLFW_MOD_SHIFT))
        {
            e.consume(this);
            draggedValue_ = index;
            if (e.mods & GLFW_MOD_ALT)
                quantizeDrag = true;
            else quantizeDrag = false;
            initX = e.pos.x;
            return;
        }
        bool wasChanged = false;
        if (index == -1)
        {
            wasChanged = true;
            float newv = rescale(e.pos.x,0,box.size.x,-5.0f,5.0f);
            v.push_back(newv);
        }
        if (e.mods & GLFW_MOD_SHIFT)
        {
            
            if (index>=0 && v.size()>1)
            {
                wasChanged = true;
                v.erase(v.begin()+index);
            }
        }
        if (wasChanged)
            qmod->updateQuantizerValues(which_,v, true);
        dirty = true;
    }
    
    void draw(const DrawArgs &args) override
    {
        if (!qmod)
            return;
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGB(0,128,0));
        nvgRect(args.vg,0,0,box.size.x,box.size.y);
        nvgFill(args.vg);
        
        auto& qvals = qmod->quantizers[which_].voltages;
        int numqvals = qvals.size();
        for (int i=0;i<numqvals;++i)
        {
            float xcor = rescale(qvals[i],-5.0f,5.0f,0.0,box.size.x);
            nvgStrokeColor(args.vg,nvgRGB(255,255,255));
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,box.size.y);
            nvgStroke(args.vg);
            
        }
        float xcor = rescale(qmod->heldOutputs[which_],-5.0f,5.0f,0.0,box.size.x);
        nvgStrokeColor(args.vg,nvgRGB(255,0,0));
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg,xcor,0);
        nvgLineTo(args.vg,xcor,box.size.y);
        nvgStroke(args.vg);
#define DEBUG_MESSAGES_QUANTIZER 0
#if DEBUG_MESSAGES_QUANTIZER
        nvgFontSize(args.vg, 15);
        nvgFontFaceId(args.vg, g_font->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        char buf[100];
        sprintf(buf,"mods %d",mousemod);
        nvgText(args.vg, 3 , 10, buf, NULL);
#endif
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
        box.size.x = 500;
        for (int i=0;i<8;++i)
        {
            addInput(createInputCentered<PJ301MPort>(Vec(30, 30+i*30), m, XQuantModule::FIRSTINPUT+i));
// #define USEFBFORQW
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
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(365, 30+i*30), m, 
                XQuantModule::AMOUNT_PARAM+i));
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(390, 30+i*30), m, 
                XQuantModule::ROTATE_PARAM+i));
            addInput(createInputCentered<PJ301MPort>(Vec(415, 30+i*30), m, XQuantModule::FIRST_ROT_CV_INPUT+i));
            addOutput(createOutputCentered<PJ301MPort>(Vec(440, 30+i*30), m, XQuantModule::FIRSTQUANOUTPUT+i));
            addOutput(createOutputCentered<PJ301MPort>(Vec(465, 30+i*30), m, XQuantModule::FIRSTGATEOUTPUT+i));
        }
        
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