#pragma once

#include <rack.hpp>
#include "plugin.hpp"

class LabelWidget : public TransparentWidget
{
public:
    enum Justification
	{
		J_LEFT,
		J_RIGHT,
		J_CENTER
	};
	LabelWidget(rack::Rect bounds,std::string txt, 
        float fontsize, NVGcolor color, Justification j) :
        m_text(txt), m_color(color), m_fontsize(fontsize), m_j(j)
    {
		box.pos = bounds.pos;
		box.size = bounds.size;
    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        nvgFontSize(args.vg, m_fontsize);
        nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
        nvgTextLetterSpacing(args.vg, -1);
        nvgFillColor(args.vg, m_color);
        float textw = nvgTextBounds(args.vg,0.0f,0.0f,m_text.c_str(),nullptr,nullptr);
		if (m_j == J_LEFT)
			nvgText(args.vg, box.pos.x , box.pos.y, m_text.c_str(), NULL);
		if (m_j == J_RIGHT)
		{
			float text_x = box.pos.x+(box.size.x-textw);
			nvgText(args.vg, text_x , box.pos.y, m_text.c_str(), NULL);
		}
		if (m_j == J_CENTER)
		{
			float text_x = box.pos.x+(box.size.x/2-textw/2);
			nvgText(args.vg, text_x , box.pos.y, m_text.c_str(), NULL);
		}
        nvgRestore(args.vg);
    }
private:
    std::string m_text;
    
    NVGcolor m_color;
    float m_fontsize = 0.0f;
	float m_xcor = 0.0f;
	float m_ycor = 0.0f;
	Justification m_j;
};

class KnobInAttnWidget : public TransparentWidget
{
public:
    KnobInAttnWidget(ModuleWidget* parent, std::string param_desc,
        int mainparamid, int cvin_id, int attnparamid, float xc, float yc, bool knobsnap=false);
	void draw(const DrawArgs &args) override;
    std::string m_labeltext;
	float m_xcor = 0.0f;
	float m_ycor = 0.0f;
};

inline KnobInAttnWidget::KnobInAttnWidget(ModuleWidget* parent, std::string param_desc,
        int mainparamid, int cvin_id, int attnparamid, float xc, float yc, bool knobsnap)
{
	m_xcor = xc;
	m_ycor = yc;
	m_labeltext = param_desc;
	box.size = Vec(80,45);
	RoundBlackKnob* knob = nullptr;
	parent->addParam(knob=createParam<RoundBlackKnob>(Vec(xc, yc+13), parent->module, mainparamid));
	knob->snap = knobsnap;
	if (cvin_id>=0)
		parent->addInput(createInput<PJ301MPort>(Vec(xc+31.0f, yc+16), parent->module, cvin_id));
	if (attnparamid>=0)
		parent->addParam(createParam<Trimpot>(Vec(xc+57.00, yc+19), parent->module, attnparamid));

}

inline void KnobInAttnWidget::draw(const DrawArgs &args)
{
	nvgSave(args.vg);
	nvgBeginPath(args.vg);
    nvgStrokeColor(args.vg, nvgRGBA(0x70, 0x70, 0x70, 0xff));
    nvgRect(args.vg,m_xcor,m_ycor,box.size.x-2,box.size.y-2);
    nvgStroke(args.vg);
	auto font = getDefaultFont(0);
	nvgFontSize(args.vg, 10);
    nvgFontFaceId(args.vg, font->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            
    nvgText(args.vg, m_xcor + 1, m_ycor + 10, m_labeltext.c_str(), NULL);
	nvgRestore(args.vg);
}


template<typename PortType>
class PortWithBackGround : public PortType
{
public:
	PortWithBackGround() : PortType()
	{

	}
	void draw(const Widget::DrawArgs &args) override
    {
        nvgSave(args.vg);
		auto backgroundcolor = nvgRGB(210,210,210);
        auto textcolor = nvgRGB(0,0,0);
        if (m_is_out)
        {
            backgroundcolor = nvgRGB(0,0,0);
            textcolor = nvgRGB(255,255,255);
        }
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, backgroundcolor);
        nvgRoundedRect(args.vg,-1.0f,-15.0f,this->box.size.x+2.0f,this->box.size.y+15.0,3.0f);
        nvgFill(args.vg);
        
        auto font = getDefaultFont(0);
        nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
        nvgFontSize(args.vg, 7.5f);
        nvgTextLetterSpacing(args.vg, 0.0f);
        nvgFillColor(args.vg, textcolor);
        nvgTextBox(args.vg,0.5f,-9.6f,this->box.size.x,m_text.c_str(),nullptr);
        nvgRestore(args.vg);
		PortType::draw(args);
	}
    std::string m_text;
    bool m_is_out = true;
};

class ZoomScrollWidget : public rack::TransparentWidget
{
public:
    ZoomScrollWidget()
    {

    }
    void draw(const Widget::DrawArgs &args) override;
    void onDragStart(const event::DragStart& e) override;
    void onDragMove(const event::DragMove& e) override;
    void onButton(const event::Button& e) override;
    std::function<void(float,float)> OnRangeChange;
    int findDragObject(float xcor,float ycor);
private:
    float m_range_start = 0.0f;
    float m_range_end = 1.0f;
    float initX = 0.0f;
    float dragX = 0.0f;
    float initDistanceFromBarStart = 0.0f;
    int dragObject = 0;
};
