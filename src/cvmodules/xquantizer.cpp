#include "plugin.hpp"
#include <random>
#include <atomic>
#include <osdialog.h>
#include <fstream>
#include "scalehelpers.h"

const int NUM_QUANTIZERS = 8;

class Quantizer
{
public:
    Quantizer()
    {
        voltages = {-5.0f,0.0f,5.0f};
        transFormedVoltages = voltages;
    }
    void sortVoltages()
    {
        std::sort(voltages.begin(),voltages.end());

    }
    
    float process(float x, float strength)
    {
        return quantize_to_grid(x,transFormedVoltages,strength);
        
    }
    int getNumVoltages() { return voltages.size(); }
    float getVoltage(int index)
    {
        return voltages[index];
    }
    void setVoltage(int index, float v)
    {
        voltages[index] = v;
    }
    float getTransformedVoltage(int index)
    {
        return transFormedVoltages[index];
    }
    void setVoltages(std::vector<float> newVoltages)
    {
        voltages = newVoltages;
        updateTransfomedVoltages();
    }
    std::vector<float> getVoltages()
    {
        return voltages;
    }
    void setRotateAmount(float amt)
    {
        if (amt!=rotateAmount)
        {
            rotateAmount = amt;
            updateTransfomedVoltages();
        }
        
    }
    int transformCount = 0;
    void updateTransfomedVoltages()
    {
        ++transformCount;
        if (transFormedVoltages.size()!=voltages.size())
            transFormedVoltages.resize(voltages.size());
        for (int i=0;i<voltages.size();++i)
        {
            transFormedVoltages[i] = wrap_value(-5.0f,voltages[i]+rotateAmount,5.0f);
        }
    }
private:
    std::vector<float> voltages;
    std::vector<float> transFormedVoltages;
    float rotateAmount = 0.0f;
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
    
    XQuantModule()
    {
        divider.setDivision(128);
        heldOutputs.resize(NUM_QUANTIZERS);
        triggerOutputs.resize(NUM_QUANTIZERS,0.0f);
        config(16,NUM_INPUTS,NUMOUTPUTS);
        for (int i=0;i<8;++i)
        {
            
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
        quantizers[quantizerindex].setVoltage(index,value);
    }
    void process(const ProcessArgs& args) override
    {
        if (divider.process())
        {
            if (shouldUpdate)
            {
                shouldUpdate = false;
                quantizers[whichQToUpdate].setVoltages(swapVector);
                whichQToUpdate = -1;
            }
            
            for (int i=0;i<8;++i)
            {
                float strength = params[AMOUNT_PARAM+i].getValue();
                float rot = params[ROTATE_PARAM+i].getValue();
                rot += inputs[FIRST_ROT_CV_INPUT+i].getVoltage();
                rot = clamp(rot,-5.0f,5.0f);
                quantizers[i].setRotateAmount(rot);
                
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
                outputs[FIRSTGATEOUTPUT+i].setVoltage(triggerOut*10.0f);
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
            for (int j=0;j<quantizers[i].getNumVoltages();++j)
            {
                float v = quantizers[i].getVoltage(j);
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
                    std::vector<float> volts;
                    for (int j=0;j<json_array_size(array2J);++j)
                    {
                        float v = json_number_value(json_array_get(array2J,j));
                        volts.push_back(v);
                    }
                    quantizers[i].setVoltages(volts);
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
                            auto& quantizer = module->quantizers[whichquant];
                            for (int i=0;i<quantizer.getNumVoltages();++i)
                            {
                                //v[i] = wrap_value(-5.0f,originalValues[i]+value,5.0f);
                            }
                            //module->updateQuantizerValues(whichquant,v,true);
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
                        //q->originalValues = mod->quantizers[which].voltages;
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
        auto& quant = qmod->quantizers[which_];
        for (int i=0;i<quant.getNumVoltages();++i)
        {
            Rect r(rescale(quant.getVoltage(i),-5.0f,5.0f,0.0,box.size.x)-2.0f,0,4.0f,
                box.size.y);
            if (r.contains(Vec{xcor,ycor}))
            {
                return i;
            }
        }
        return -1;
    }
    void onDragStart(const event::DragStart& e) override
    {
        dragX = APP->scene->rack->getMousePos().x;
    }
    void onDragEnd(const event::DragEnd &e) override
    {

    }
    int mousemod = 0;
    float clampValue(Quantizer& quant, int index, float input, float minval, float maxval)
    {
        if (index == 0)
            return clamp(input,minval,quant.getVoltage(1)-0.01);
        if (index == quant.getNumVoltages()-1)
            return clamp(input,quant.getVoltage(index-1)+0.01,maxval);
        int leftIndex = index - 1;
        int rightIndex = index + 1;
        return clamp(input,quant.getVoltage(leftIndex)+0.01,quant.getVoltage(rightIndex)-0.01);
        
    }
    void onDragMove(const event::DragMove& e) override
    {
        if (draggedValue_==-1)
            return;
        auto& quant = qmod->quantizers[which_];
        float newDragX = APP->scene->rack->getMousePos().x;
        float newPos = initX+(newDragX-dragX);
        float val = rescale(newPos,0.0f,box.size.x,-5.0,5.0);
        if (quantizeDrag)
        {
            int temp = val * 6.0f;
            val = temp / 6.0f;
        }
        val = clampValue(quant,draggedValue_,val,-5.0f,5.0f);
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
        
        

        struct GenerateScaleMenuItem : MenuItem
		{
			QuantizeValuesWidget* w = nullptr;
            int scaleType = 0;
            void onAction(const event::Action &e) override
			{
				if (!w)
                    return;
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
                } else if (scaleType == 2)
                {
                    int harm = 1;
                    while (true)
                    {
                        float volt = customlog(2.0,harm)-5.0f;
                        if (volt<=0.0f)
                        {
                            v.push_back(volt);
                        } else
                            break;
                        ++harm;
                    }
                } else if (scaleType == 3)
                {
                    std::vector<float> greek{
                        1.0,
                        28.0/27.0,
                        16.0/15.0,
                        4.0/3.0,
                        3.0/2.0,
                        14.0/9.0,
                        8.0/5.0
                        };
 

                    for (int i=0;i<11;++i)
                    {
                        for (int j=0;j<greek.size();++j)
                        {
                            float oct = -5.0+i*1.0f;
                            float pitch = customlog(2.0,greek[j]);
                            if (oct+pitch<=5.0f)
                                v.push_back(oct+pitch);
                        }
                        
                    }
                } else if (scaleType == 4)
                {
                    std::vector<float> gamelan{
                        1/1,
                        10/9,
                        7.0/6,
                        32.0/25,
                        47.0/35,
                        32.0/23,
                        3.0/2,
                        20.0/13,
                        16.0/9,
                        16.0/9,
                        23.0/12

                        };
 

                    for (int i=0;i<11;++i)
                    {
                        for (int j=0;j<gamelan.size();++j)
                        {
                            float oct = -5.0+i*1.0f;
                            float pitch = customlog(2.0,gamelan[j]);
                            if (oct+pitch<=5.0f)
                                v.push_back(oct+pitch);
                        }
                        
                    }
                }
                w->qmod->updateQuantizerValues(w->which_,v,true);
			}
		};
        struct LoadScalaFileItem  : MenuItem
        {
            QuantizeValuesWidget* qw = nullptr;
            void onAction(const event::Action &e) override
            {
                if (qw==nullptr)
                    return;
                std::string dir = asset::plugin(pluginInstance, "/scala_scales");
                osdialog_filters* filters = osdialog_filters_parse("Scala File:scl");
	            //pathC = osdialog_file(OSDIALOG_OPEN, "C:/", NULL, filters);
	            
                //std::string dir("/Users/teemu/Documents/Rack/plugins-v1/NYSTHI/res/microtuning/scala_scales");
                
                char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
		        osdialog_filters_free(filters);
                if (!pathC) {
			        return;
		        }
		        std::string path = pathC;
		        std::free(pathC);
                auto result = loadScala(path);
                if (result.size()>0)
                {
                    qw->qmod->updateQuantizerValues(qw->which_,result,false);
                }
            }

        };
        struct GenerateScalesItem : MenuItem
        {
            QuantizeValuesWidget* qw = nullptr;
            Menu *createChildMenu() override 
            {
		        Menu *submenu = new Menu();
                std::vector<std::string> scaleNames={"Octaves","Major","Natural Harmonics","Greek Enharmonic",
                    "Gamelan Udan"};
		        for (int i = 0; i < scaleNames.size(); i++) 
                {
			        GenerateScaleMenuItem *item = createMenuItem<GenerateScaleMenuItem>(scaleNames[i]);
			        item->w = qw;
			        item->scaleType = i;
			        submenu->addChild(item);
                }
                return submenu;
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
            
            GenerateScalesItem* scalesItem = createMenuItem<GenerateScalesItem>("Generate scale",RIGHT_ARROW);
		    scalesItem->qw = this;
		    menu->addChild(scalesItem);
            auto loadScalaMenuItem = createMenuItem<LoadScalaFileItem>("Load Scala file...");
            loadScalaMenuItem->qw = this;
            menu->addChild(loadScalaMenuItem);
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
        auto v = qmod->quantizers[which_].getVoltages();
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
        
        auto& quant = qmod->quantizers[which_];
        int numqvals = quant.getNumVoltages();
        for (int i=0;i<numqvals;++i)
        {
            float xcor = rescale(quant.getVoltage(i),-5.0f,5.0f,0.0,box.size.x);
            nvgStrokeColor(args.vg,nvgRGB(255,255,255));
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,box.size.y*0.74);
            nvgStroke(args.vg);

            xcor = rescale(quant.getTransformedVoltage(i),-5.0f,5.0f,0.0,box.size.x);
            nvgStrokeColor(args.vg,nvgRGB(200,200,200));
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg,xcor,box.size.y*0.75);
            nvgLineTo(args.vg,xcor,box.size.y);
            nvgStroke(args.vg);
        }
        float xcor = rescale(qmod->heldOutputs[which_],-5.0f,5.0f,0.0,box.size.x);
        nvgStrokeColor(args.vg,nvgRGB(255,0,0));
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg,xcor,box.size.y*0.75);
        nvgLineTo(args.vg,xcor,box.size.y);
        nvgStroke(args.vg);
#define DEBUG_MESSAGES_QUANTIZER 1
#if DEBUG_MESSAGES_QUANTIZER
        nvgFontSize(args.vg, 15);
        nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
        char buf[100];
        sprintf(buf,"transforms %d",quant.transformCount);
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
        nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
        char buf[100];
        sprintf(buf,"XQuantizer");
        nvgText(args.vg, 3 , 10, buf, NULL);
        nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }

};

Model* modelXQuantizer = createModel<XQuantModule, XQuantWidget>("XQuantizer");
