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
    if (leftEdgeBox.contains({xcor,ycor}))
        return 1;
    float x1 = rescale(m_range_end,0.0f,1.0f,0.0f,box.size.x);
    Rect rightEdgeBox(x1-10.0f,0.0f,10.0f,box.size.y);
    if (rightEdgeBox.contains({xcor,ycor}))
        return 2;
    Rect middleBox(x0+10.0f,0.0f,(x1-x0)-20.0f,box.size.y);
    if (middleBox.contains({xcor,ycor}))
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
    dragX = APP->scene->rack->mousePos.x;
}

void ZoomScrollWidget::onDragMove(const event::DragMove& e)
{
    if (dragObject == 0)
        return;
    float newDragX = APP->scene->rack->mousePos.x;
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
