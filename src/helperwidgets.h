#pragma once

#include <rack.hpp>
#include "plugin.hpp"

class MyRoundBlackKnob : public RoundBlackKnob
{
public:
    MyRoundBlackKnob() : RoundBlackKnob() {}
    void randomize() override 
    {
        
    }
};

class MyTrimPot : public Trimpot
{
public:
    MyTrimPot() : Trimpot() {}
    void randomize() override 
    {
        
    }
};

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
        int mainparamid, int cvin_id, int attnparamid, float xc, float yc, bool knobsnap=false, float labfontsize=10.0f);
	void draw(const DrawArgs &args) override;
    std::string m_labeltext;
	float m_xcor = 0.0f;
	float m_ycor = 0.0f;
    float m_labfontsize = 10.0f;
};

class PortWithBackGround : public TransparentWidget
{
public:
	PortWithBackGround(Module* m, ModuleWidget* mw, int portNumber, int xpos, int ypos,
        std::string name,bool isOutput);
	void draw(const Widget::DrawArgs &args) override;
    
    std::string m_text;
    bool m_is_out = true;
    PJ301MPort* portWidget = nullptr;
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
