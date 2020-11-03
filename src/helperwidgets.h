#pragma once

#include <rack.hpp>

template<typename OutPortType>
class OutPortWithBackGround : public OutPortType
{
public:
	OutPortWithBackGround() : OutPortType()
	{

	}
	void draw(const Widget::DrawArgs &args) override
    {
        
		nvgSave(args.vg);
		nvgBeginPath(args.vg);
    	nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
    	nvgRoundedRect(args.vg,-1.0f,-1.0f,this->box.size.x+2.0f,this->box.size.y+2.0f,3.0f);
    	nvgFill(args.vg);
		nvgRestore(args.vg);
		OutPortType::draw(args);
	}
};
