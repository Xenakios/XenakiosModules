#pragma once

#include <rack.hpp>
#include "plugin.hpp"

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
		if (m_is_out)
        {
            nvgBeginPath(args.vg);
    	    nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
    	    nvgRoundedRect(args.vg,-1.0f,-15.0f,this->box.size.x+2.0f,this->box.size.y+15.0,3.0f);
    	    nvgFill(args.vg);
        }
        
		auto font = getDefaultFont(0);
        nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
        nvgFontSize(args.vg, 7.5f);
        nvgTextLetterSpacing(args.vg, 1.0f);
        nvgFillColor(args.vg, nvgRGB(255,255,255));
        nvgTextBox(args.vg,0.5f,-9.6f,this->box.size.x,m_text.c_str(),nullptr);
        nvgRestore(args.vg);
		PortType::draw(args);
	}
    std::string m_text;
    bool m_is_out = true;
};
