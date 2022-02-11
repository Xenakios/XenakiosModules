#include "../plugin.hpp"
#include "../helperwidgets.h"

class TimeSeqEvent
{
public:
    TimeSeqEvent() {}
    TimeSeqEvent(double t, double d) : m_time(t), m_dur(d) {}
    double m_time = 0.0;
    double m_dur = 0.0;
};

class TimeSeqEngine
{
public:
    TimeSeqEngine()
    {
        m_events.resize(65536);
    }
    void generateEvents()
    {
        int numevents = clamp((int)(m_dur * m_density),1,m_events.size());
        m_num_events = numevents;
        for (int i=0;i<numevents;++i)
        {
            float normtime = 0.0f;
            if (m_algo == 0)
            {
                normtime = rescale((float)i,0,numevents,0.0f,1.0f);
                if (m_par1<0.5f)
                {
                    float d = rescale(m_par1,0.0f,0.5f,4.0f,1.0f);
                    normtime = std::pow(normtime,d);
                } else
                {
                    float d = rescale(m_par1,0.5f,1.0f,1.0f,4.0f);
                    normtime = 1.0f-std::pow(1.0f-normtime,d);
                }
            }
                
            else
                normtime = rack::random::uniform();
            normtime = rescale(normtime,0.0f,1.0f,m_start_time,m_end_time);
            float t = m_dur * normtime;
            m_events[i] = {t,0.0};
        }
        std::sort(m_events.begin(),m_events.begin()+numevents,
            [](const TimeSeqEvent& a, const TimeSeqEvent& b){ return a.m_time<b.m_time; });
        for (int i=0;i<numevents-1;++i)
        {
            double dur = m_events[i+1].m_time-m_events[i].m_time;
            m_events[i].m_dur = dur;
        }
        m_events[numevents-1].m_dur = m_dur - m_events[numevents-1].m_time;
        m_cur_event = 0;
        m_seqphase = 0.0;
        
    }
    void process(float deltatime,float& out,float& eoc)
    {
        if (m_seqphase >= m_dur || m_cur_event >= m_num_events)
        {
            out = 0.0f;
            eoc = 0.0f;
            return;
        }
            
        auto& ev = m_events[m_cur_event];
        double ph = ev.m_time - m_seqphase;
        double ev_gate_end = ev.m_time+ev.m_dur*m_gatelen;
        double ev_end = ev.m_time+ev.m_dur;
        if (m_seqphase>=ev.m_time && m_seqphase<ev_gate_end)
            out = 1.0f;
        else out = 0.0f;
        
        m_seqphase += deltatime;
        eoc = 0.0f;
        if (m_seqphase>=ev_end)
        {
            ++m_cur_event;
            if (m_cur_event == m_num_events)
                eoc = 1.0f;
            if (m_doUpdate)
            {
                generateEvents();
                m_doUpdate = false;
            }
                
        }
        
    }
    void setDuration(float d)
    {
        m_dur = clamp(d,0.5f,60.0f);
    }
    void setDensity(float d)
    {
        m_density = clamp(d,0.1f,32.0f);
    }
    void setAlgo(int a)
    {
        m_algo = clamp(a,0,1);
    }
    void setPar1(float p)
    {
        m_par1 = clamp(p,0.0f,1.0f);
    }
    void setPar2(float p)
    {
        m_par2 = clamp(p,0.0f,1.0f);
    }
    void setActiveRange(float t0, float t1)
    {
        m_start_time = clamp(t0,0.0f,1.0f);
        m_end_time = clamp(t1,0.0f,1.0f);
        if (m_end_time<m_start_time)
            std::swap(m_start_time,m_end_time);
        if (m_end_time-m_start_time<0.01)
            m_end_time += 0.01;
    }
    void update()
    {
        m_doUpdate = true;
    }
//private:
    std::vector<TimeSeqEvent> m_events;
    float m_dur = 5.0f;
    float m_density = 4.0f;
    float m_gatelen = 0.5f;
    float m_start_time = 0.0f;
    float m_end_time = 1.0f;
    int m_algo = 0;
    float m_par1 = 0.5f;
    float m_par2 = 0.5f;
    int m_cur_event = 0;
    int m_num_events = 0;
    double m_seqphase = 0.0;
    bool m_doUpdate = false;
};

class TimeSeqModule : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_SEQDUR,
        PAR_SEQDENSITY,
        PAR_ALGO,
        PAR_PAR1,
        PAR_PAR2,
        PAR_SEQ_START,
        PAR_SEQ_END,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_TRIG,
        IN_DENSITY,
        IN_DURATION,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_GATE,
        OUT_EOC,
        OUT_LAST
    };
    TimeSeqModule()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_SEQDUR,0.5f,60.0f,5.0f,"Sequence duration");
        configParam(PAR_SEQDENSITY,-2.f, 6.f, 1.f, "Density", " events per second", 2, 1);
        configParam(PAR_ALGO,0.0f,1.0f,0.0f,"Sequence algorithm");
        getParamQuantity(PAR_ALGO)->snapEnabled = true;
        configParam(PAR_PAR1,0.0f,1.0f,0.5f,"PAR 1");
        configParam(PAR_SEQ_START,0.0f,1.0f,0.0f,"EVENTS START");
        configParam(PAR_SEQ_END,0.0f,1.0f,1.0f,"EVENTS END");
    }
    void process(const ProcessArgs& args) override
    {
        float density = params[PAR_SEQDENSITY].getValue();
        density += inputs[IN_DENSITY].getVoltage()*0.5f;
        density = std::pow(2.0f,density);
        m_eng.setDensity(density);
        float dur = params[PAR_SEQDUR].getValue();
        dur += inputs[IN_DURATION].getVoltage()*0.5f;
        m_eng.setDuration(dur);
        int algo = params[PAR_ALGO].getValue();
        m_eng.setAlgo(algo);
        float p1 = params[PAR_PAR1].getValue();
        m_eng.setPar1(p1);
        float t0 = params[PAR_SEQ_START].getValue();
        float t1 = params[PAR_SEQ_END].getValue();
        m_eng.setActiveRange(t0,t1);
        if (m_trig.process(inputs[IN_TRIG].getVoltage()))
        {
            m_eng.generateEvents();
        }
        float out = 0.0f;
        float eoc = 0.0f;
        m_eng.process(args.sampleTime,out,eoc);
        outputs[OUT_GATE].setVoltage(out*10.0f);
        
    }
    TimeSeqEngine m_eng;
    dsp::SchmittTrigger m_trig;
};

class TimeSeqWidget : public rack::ModuleWidget
{
public:
    TimeSeqWidget(TimeSeqModule* m)
    {
        setModule(m);
        box.size.x = RACK_GRID_WIDTH * 12;
        new PortWithBackGround(m,this,TimeSeqModule::IN_TRIG,1.0f,15.0f,"TRIG",false);
        new PortWithBackGround(m,this,TimeSeqModule::OUT_GATE,30.0f,15.0f,"GATE",true);
        float xc = 2.0f;
        float yc = 60.0f;
        addChild(new KnobInAttnWidget(this,"RATE",TimeSeqModule::PAR_SEQDENSITY,
            TimeSeqModule::IN_DENSITY,-1,xc,yc,false));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"DURATION",TimeSeqModule::PAR_SEQDUR,
            TimeSeqModule::IN_DURATION,-1,xc,yc,false));
        xc = 2.0f;
        yc += 47;
        addChild(new KnobInAttnWidget(this,"ALGO",TimeSeqModule::PAR_ALGO,
            -1,-1,xc,yc,false));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"PAR 1",TimeSeqModule::PAR_PAR1,
            -1,-1,xc,yc,false));
        xc = 2.0f;
        yc += 47;
        addChild(new KnobInAttnWidget(this,"EVENTS START",TimeSeqModule::PAR_SEQ_START,
            -1,-1,xc,yc,false));
        xc += 82;
        addChild(new KnobInAttnWidget(this,"EVENTS END",TimeSeqModule::PAR_SEQ_END,
            -1,-1,xc,yc,false));
    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x50, 0x50, 0x50, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelTimeSeq = createModel<TimeSeqModule, TimeSeqWidget>("XTimeSeq");
