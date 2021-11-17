#include "helperwidgets.h"

void ZoomScrollWidget::draw(const Widget::DrawArgs &args)
{
    nvgSave(args.vg);
    bool horiz = true;
    if (box.size.y>box.size.x)
        horiz = false;
    nvgBeginPath(args.vg);
    nvgFillColor(args.vg, nvgRGB(200,200,200));
    nvgRect(args.vg,0.0f,0.0f,box.size.x,box.size.y);
    nvgFill(args.vg);
    nvgBeginPath(args.vg);
    nvgFillColor(args.vg, nvgRGB(120,120,120));
    float xcor0 = rescale(m_range_start,0.0f,1.0f,0.0f,box.size.x);
    float xcor1 = rescale(m_range_end,0.0f,1.0f,0.0f,box.size.x);
    nvgRoundedRect(args.vg,xcor0,0.0f,xcor1-xcor0,box.size.y,5.0f);
    nvgFill(args.vg);
    nvgRestore(args.vg);
}

int ZoomScrollWidget::findDragObject(float xcor,float ycor)
{
    float x0 = rescale(m_range_start,0.0f,1.0f,0.0f,box.size.x);
    Rect leftEdgeBox(x0,0.0f,10.0f,box.size.y);
    if (leftEdgeBox.contains(Vec{xcor,ycor}))
        return 1;
    float x1 = rescale(m_range_end,0.0f,1.0f,0.0f,box.size.x);
    Rect rightEdgeBox(x1-10.0f,0.0f,10.0f,box.size.y);
    if (rightEdgeBox.contains(Vec{xcor,ycor}))
        return 2;
    Rect middleBox(x0+10.0f,0.0f,(x1-x0)-20.0f,box.size.y);
    if (middleBox.contains(Vec{xcor,ycor}))
        return 3;
    return 0;
}

void ZoomScrollWidget::onButton(const event::Button& e)
{
    if (e.action == GLFW_RELEASE) // || rightClickInProgress)
    {
        dragObject = 0;
        return;
    }
    int index = findDragObject(e.pos.x,e.pos.y);
        
    if (index>0 && !(e.mods & GLFW_MOD_SHIFT) && e.button == GLFW_MOUSE_BUTTON_LEFT)
    {
        e.consume(this);
        dragObject = index;
        initX = e.pos.x;
        float x0 = rescale(e.pos.x,0,box.size.x,0.0f,1.0f);
        initDistanceFromBarStart = x0-m_range_start;
        return;
    }
    if (index == 0)
    {
        dragObject = 0;
        e.consume(this);
    }
}

void ZoomScrollWidget::onDragStart(const event::DragStart& e)
{
    dragX = APP->scene->rack->getMousePos().x;
}

void ZoomScrollWidget::onDragMove(const event::DragMove& e)
{
    if (dragObject == 0)
        return;
    float newDragX = APP->scene->rack->getMousePos().x;
    float newPosX = initX+(newDragX-dragX);
    float xp = rescale(newPosX,0.0f,box.size.x,0.0f,1.0f);
    xp = clamp(xp,0.0f,1.0f);
    if (dragObject == 1)
        m_range_start = xp;
    if (dragObject == 2)
        m_range_end = xp;
    if (dragObject == 3)
    {
        float range = m_range_end-m_range_start;
        float newStart = xp-initDistanceFromBarStart;
        float newEnd = newStart+range;
        if (newStart>=0.0 && newEnd<=1.0f)
        {
            m_range_start = newStart;
            m_range_end = newEnd;
        }
        
    }
    if (m_range_end<m_range_start+0.05)
        m_range_end = m_range_start+0.05;
    if (OnRangeChange)
        OnRangeChange(m_range_start,m_range_end);
}

PortWithBackGround::PortWithBackGround(Module* m, ModuleWidget* mw, int portNumber, int xpos, int ypos,
        std::string name,bool isOutput) : m_text(name),m_is_out(isOutput)
{
    box.pos.x = xpos;
    box.pos.y = ypos;
    box.size.x = 27;
    box.size.y = 40;
    mw->addChild(this);
    if (isOutput)
        mw->addOutput(createOutput<PJ301MPort>(Vec(xpos+0.5, ypos+14.5), m, portNumber));
    else
        mw->addInput(createInput<PJ301MPort>(Vec(xpos+0.5, ypos+14.5), m, portNumber));
    
}

void PortWithBackGround::draw(const Widget::DrawArgs &args) 
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
    nvgRoundedRect(args.vg,0.0f,0.0f,box.size.x,box.size.y,3.0f);
    nvgFill(args.vg);
    
    auto font = getDefaultFont(0);
    nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
    nvgFontSize(args.vg, 7.5f);
    nvgTextLetterSpacing(args.vg, 0.1f);
    nvgFillColor(args.vg, textcolor);
    nvgTextBox(args.vg,1.0f,6.0,box.size.x-1.0f,m_text.c_str(),nullptr);
    nvgRestore(args.vg);
    //PortType::draw(args);
}

KnobInAttnWidget::KnobInAttnWidget(ModuleWidget* parent, std::string param_desc,
        int mainparamid, int cvin_id, int attnparamid, float xc, float yc, bool knobsnap, float labfontsize)
{
	m_xcor = xc;
	m_ycor = yc;
	m_labfontsize = labfontsize;
    m_labeltext = param_desc;
	box.size = Vec(80,45);
	MyRoundBlackKnob* knob = nullptr;
	parent->addParam(knob=createParam<MyRoundBlackKnob>(Vec(xc+1.0, yc+13), parent->module, mainparamid));
	knob->snap = knobsnap;
	m_knob = knob;
    if (cvin_id>=0)
		parent->addInput(createInput<PJ301MPort>(Vec(xc+31.0f, yc+16), parent->module, cvin_id));
	if (attnparamid>=0)
    {
		parent->addParam(m_trimpot = createParam<MyTrimPot>(Vec(xc+57.00, yc+19), parent->module, attnparamid));

    }

}

inline void KnobInAttnWidget::draw(const DrawArgs &args)
{
	nvgSave(args.vg);
	nvgBeginPath(args.vg);
    nvgStrokeColor(args.vg, nvgRGBA(0x70, 0x70, 0x70, 0xff));
    nvgRect(args.vg,m_xcor,m_ycor,box.size.x-2,box.size.y-2);
    nvgStroke(args.vg);
	auto font = getDefaultFont(0);
	nvgFontSize(args.vg, m_labfontsize);
    nvgFontFaceId(args.vg, font->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            
    nvgText(args.vg, m_xcor + 1, m_ycor + 10, m_labeltext.c_str(), NULL);
	nvgRestore(args.vg);
}

