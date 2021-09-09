#include "gendyn.h"
#include "helperwidgets.h"

extern std::shared_ptr<Font> g_font;

inline double custom_log(double value, double base)
{
    return std::log(value)/std::log(base);
}

inline void sanitizeRange(float& a, float& b, float mindiff)
{
	if (b<a)
		std::swap(a,b);
	if (b-a<mindiff)
		b+=mindiff;
}

enum Distributions
	{
		DIST_Uniform,
		DIST_Gauss,
		DIST_Cauchy,
		LASTDIST
	};
enum ResetModes
{
	RM_Zeros,
	RM_Avg,
	RM_Min,
	RM_Max,
	RM_UniformRandom,
	RM_BinaryRandom,
	LASTRM
};




inline float avg(float a, float b)
{
	return a + (b - a) / 2.0f;
}

class GendynNode
{
public:
	GendynNode() {}
	float m_x_prim = 0.0f;
	float m_y_prim = 0.0f;
	float m_x_sec = 0.0f;
	float m_y_sec = 0.0f;
};

class GendynOsc
{
public:
	GendynOsc()
	{
		m_nodes.resize(128);
		for (int i = 0; i < 128; ++i)
		{
			m_nodes[i].m_x_prim = avg(m_time_primary_high_barrier, m_time_primary_high_barrier);
			m_nodes[i].m_x_sec = avg(m_time_secondary_low_barrier, m_time_secondary_high_barrier);

		}
		m_cur_dur = m_nodes.front().m_x_sec;
		m_cur_y0 = m_nodes.front().m_y_sec;
		m_cur_y1 = m_nodes[1].m_y_sec;
		m_next_segment_time = m_nodes[0].m_x_sec;
        setSampleRate(44100.0f);
	}
	void setRandomSeed(int s)
	{
		m_rand = std::mt19937(s);
	}
	void process(float* buf, int nframes)
	{
		for (int i = 0; i < nframes; ++i)
		{
			float t1 = m_cur_dur;
			
			float y0 = m_cur_y0;
			float y1 = m_cur_y1;
			float s = y0 + (y1 - y0) / t1 * m_phase;
			s = m_hpfilt.process(s);
            buf[i] = s; //clamp(s,-1.0f,1.0f);
			m_phase += 1.0;
			//m_segment_phase += 1.0;
			if (m_phase >= m_next_segment_time)
			{
				++m_cur_node;
				
				if (m_cur_node < m_num_segs - 1)
				{
					m_cur_dur = m_nodes[m_cur_node].m_x_sec;
					m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
					m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
					m_next_segment_time = m_cur_dur;
				}
				if (m_cur_node == m_num_segs - 1)
				{
					m_cur_dur = m_nodes[m_cur_node].m_x_sec;
					m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
					m_next_segment_time = m_cur_dur;
					updateTable();
					m_cur_y1 = m_nodes.front().m_y_sec;
                    m_cur_node = 0;
				}
                /*
				if (m_cur_node == m_num_segs)
				{
					m_cur_node = 0;
					m_cur_dur = m_nodes[m_cur_node].m_x_sec;
					m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
					m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
					m_next_segment_time = m_cur_dur;
					
				}
                */
				m_phase = 0.0;
			}
		}
	}
	void resetTable()
	{
		std::uniform_real_distribution<float> ampdist{m_amp_secondary_low_barrier,m_amp_secondary_high_barrier};
		std::uniform_real_distribution<float> timedist{m_time_secondary_low_barrier,m_time_secondary_high_barrier};
		std::uniform_real_distribution<float> unidist(0.0,1.0);
		for (int i = 0; i < m_num_segs; ++i)
		{
			m_nodes[i].m_x_prim = avg(m_time_primary_low_barrier,m_time_primary_high_barrier);
			if (m_timeResetMode == RM_Avg)
				m_nodes[i].m_x_sec = avg(m_time_secondary_low_barrier,m_time_secondary_high_barrier);
			else if (m_timeResetMode == RM_BinaryRandom)
			{
				if (unidist(m_rand)<0.5)
					m_nodes[i].m_x_sec = m_time_secondary_low_barrier;
				else m_nodes[i].m_x_sec = m_time_secondary_high_barrier;
			}
			else
			{
				m_nodes[i].m_x_sec = m_sampleRate/m_center_frequency/m_num_segs;
			}
			m_nodes[i].m_y_prim = 0.0f;
			if (m_ampResetMode == RM_Zeros)
				m_nodes[i].m_y_sec = 0.0f;
			else if (m_ampResetMode == RM_UniformRandom)
				m_nodes[i].m_y_sec = ampdist(m_rand);
			else
				m_nodes[i].m_y_sec = 0.0f;
		}
		m_cur_node = 0;
        m_phase = 0.0;
		m_next_segment_time = m_nodes[0].m_x_sec;
		//m_segment_phase = 0.0;
		m_cur_dur = m_nodes[m_cur_node].m_x_sec;
		m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
		m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
	}
	void setFrequencies(float center, float a, float b)
	{
		m_center_frequency = center;
		float hz = center*pow(2.0,(1.0/12.0*a));
		m_low_frequency = hz;
		m_time_secondary_high_barrier = clamp(m_sampleRate/hz/m_num_segs,1.0f,128.0f);
		hz = center*pow(2.0,(1.0/12.0*b));
		m_high_frequency = hz;
		m_time_secondary_low_barrier = clamp(m_sampleRate/hz/m_num_segs,1.0,128.0f);
		sanitizeRange(m_time_secondary_low_barrier,m_time_secondary_high_barrier,1.0f);
	}
	void updateTable()
	{
		if (m_amp_flux < 0.25f)
        {
            float norm = rescale(m_amp_flux,0.0f,0.25f,0.0f,1.0f);
            //m_amp_dev = norm*
        } else if (m_amp_flux>=0.25f && m_amp_flux<0.5f)
        {

        } else if (m_amp_flux>=0.5f && m_amp_flux<0.75f)
        {

        } else 
        {

        }
        m_amp_dev = m_amp_flux * (m_amp_primary_high_barrier-m_amp_primary_low_barrier);
        std::normal_distribution<float> timedist(m_time_mean, m_time_dev);
		std::normal_distribution<float> ampdist(m_amp_mean, m_amp_dev);
		float segAcc = 0.0f;
		for (int i = 0; i < m_num_segs; ++i)
		{
			float x_p = m_nodes[i].m_x_prim;
			x_p += timedist(m_rand);
			x_p = reflect_value(m_time_primary_low_barrier, x_p,m_time_primary_high_barrier);
			float x_s = m_nodes[i].m_x_sec;
			x_s += x_p;
			float secbar0 = m_time_secondary_low_barrier;
			float secbar1 = m_time_secondary_high_barrier;
			sanitizeRange(secbar0,secbar1,1.0f);
			x_s = reflect_value(secbar0, x_s, secbar1);
			m_nodes[i].m_x_prim = x_p;
			m_nodes[i].m_x_sec = x_s;
			segAcc+=m_nodes[i].m_x_sec;
			float y_p = m_nodes[i].m_y_prim;
			y_p += ampdist(m_rand);
			y_p = clamp(y_p,m_amp_primary_low_barrier, m_amp_primary_high_barrier);
			float y_s = m_nodes[i].m_y_sec;
			y_s += y_p;
			y_s = reflect_value(m_amp_secondary_low_barrier, y_s, m_amp_secondary_high_barrier);
			m_nodes[i].m_y_prim = y_p;
			m_nodes[i].m_y_sec = y_s;
		}
		float freq = m_sampleRate/segAcc;
		float volts = custom_log(freq/rack::dsp::FREQ_C4,2.0f);
        m_curFrequencyVolts = clamp(volts,-5.0,5.0);
		//m_next_segment_time = m_nodes[0].m_x_sec;
	}
	int m_num_segs = 11;
	float m_time_primary_low_barrier = -1.0;
	float m_time_primary_high_barrier = 1.0;
	float m_time_secondary_low_barrier = 5.0;
	float m_time_secondary_high_barrier = 20.0;
	float m_time_mean = 0.0f;
	float m_time_dev = 0.01;
	
	int m_timeResetMode = RM_Avg;
	int m_ampResetMode = RM_Zeros;
	float m_center_frequency = 440.0f;
	float m_low_frequency = 440.0f;
	float m_high_frequency = 440.0f;
	float m_curFrequencyVolts = 0.0f;
    void setNumSegments(int n)
    {
        if (n!=m_num_segs)
        {
            m_num_segs = clamp(n,3,64);
            //if (m_num_segs>=m_cur_node)
			{
				m_cur_node = 0;
            	m_phase = 0.0;
				m_cur_dur = m_nodes[m_cur_node].m_x_sec;
				m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
				m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
			}
        }
    }
	void setSampleRate(float s)
	{
		if (s!=m_sampleRate)
		{
			m_sampleRate = s;
			float normfreq = 25.0/m_sampleRate;
            float q = sqrt(2.0)/2.0;
            m_hpfilt.setParameters(rack::dsp::BiquadFilter::HIGHPASS,normfreq,q,1.0f);
		}
	}
	void setAmplitudeFlux(float f)
    {
        f = clamp(f,0.0f,1.0f);
        m_amp_flux = f;
    }
private:
	int m_cur_node = 0;
	double m_phase = 0.0;
	//double m_segment_phase = 0.0;
	double m_next_segment_time = 0.0;
	std::vector<GendynNode> m_nodes;
	std::mt19937 m_rand;
	float m_cur_dur = 0.0;
	float m_cur_y0 = 0.0;
	float m_cur_y1 = 0.0;
	float m_sampleRate = 0.0f;
    float m_amp_primary_low_barrier = -0.05;
	float m_amp_primary_high_barrier = 0.05;
	float m_amp_secondary_low_barrier = -0.9;
	float m_amp_secondary_high_barrier = 0.9;
	float m_amp_mean = 0.0f;
	float m_amp_dev = 0.01;
    float m_amp_flux = -1.0f;
	dsp::TBiquadFilter<float> m_hpfilt;
};

class GendynModule : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_NUM_SEGS,
		PAR_TIME_DISTRIBUTION,
		PAR_TIME_RESET_MODE,
        PAR_TimePrimaryBarrierLow,
        PAR_TimePrimaryBarrierHigh,
        PAR_TimeSecondaryBarrierLow,
        PAR_TimeSecondaryBarrierHigh,
        PAR_TimeMean,
        PAR_TimeDeviation,
        PAR_AMP_RESET_MODE,
        PAR_AMP_BEHAVIOR,
		PAR_PolyphonyVoices,
		PAR_CenterFrequency,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_RESET,
        IN_PITCH,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_AUDIO,
        OUT_PITCH,
        OUT_LAST
    };
	int m_numvoices_used = 0;
    GendynModule();
    std::string getDebugMessage();
    void process(const ProcessArgs& args) override;
private:
    GendynOsc m_oscs[16];
	dsp::SchmittTrigger m_reset_trigger;
	dsp::ClockDivider m_divider;
};

class GendynWidget : public ModuleWidget
{
public:
    GendynWidget(GendynModule* m);
    void draw(const DrawArgs &args) override;
};


GendynModule::GendynModule()
{
    for (int i=0;i<16;++i)
        m_oscs[i].setRandomSeed(i);
    config(PARAMS::PAR_LAST,IN_LAST,OUT_LAST);
    configParam(PAR_NUM_SEGS,3.0,64.0,10.0,"Num segments");
    configParam(PAR_TIME_DISTRIBUTION,0.0,LASTDIST-1,1.0,"Time distribution");
    configParam(PAR_TimeMean,-5.0,5.0,0.0,"Time mean");
    configParam(PAR_TIME_RESET_MODE,0.0,LASTRM,RM_Avg,"Time reset mode");
    configParam(PAR_TimeDeviation,0.0,5.0,0.1,"Time deviation");
    configParam(PAR_TimePrimaryBarrierLow,-5.0,5.0,-1.0,"Time primary low barrier");
    configParam(PAR_TimePrimaryBarrierHigh,-5.0,5.0,1.0,"Time primary high barrier");
    configParam(PAR_TimeSecondaryBarrierLow,-60.0,60.0,-1.0,"Time sec low barrier");
    configParam(PAR_TimeSecondaryBarrierHigh,-60.0,60.0,1.0,"Time sec high barrier");
    configParam(PAR_AMP_RESET_MODE,0.0,LASTRM,RM_UniformRandom,"Amp reset mode");
    configParam(PAR_PolyphonyVoices,0.0,16.0,0,"Polyphony voices");
    configParam(PAR_CenterFrequency,-54.f, 54.f, 0.f, "Center frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
    configParam(PAR_AMP_BEHAVIOR,0.0,1.0f,0.1f,"Amplitude flux");
    m_divider.setDivision(16);
}

std::string GendynModule::getDebugMessage()
{
    std::stringstream ss;
    ss << m_oscs[0].m_low_frequency << " " << m_oscs[0].m_center_frequency << " ";
    ss << m_oscs[0].m_high_frequency << " " << m_oscs[0].m_time_secondary_low_barrier << " ";
    ss << m_oscs[0].m_time_secondary_high_barrier << " " << m_numvoices_used;
    return ss.str();
    
}

void GendynModule::process(const ProcessArgs& args)
{
    int numvoices = params[PAR_PolyphonyVoices].getValue();
    if (numvoices == 0 && inputs[IN_PITCH].isConnected())
        numvoices = inputs[IN_PITCH].getChannels();
    if (numvoices == 0)
        numvoices = 1;
    bool shouldReset = false;
    if (m_reset_trigger.process(inputs[IN_RESET].getVoltage()))
    {
        shouldReset = true;
    }
    m_numvoices_used = numvoices;
    outputs[0].setChannels(numvoices);
    outputs[1].setChannels(numvoices);
    float numsegs = params[PAR_NUM_SEGS].getValue();
    numsegs = clamp(numsegs,3.0,64.0);
    float timedev = params[PAR_TimeDeviation].getValue();
    timedev = clamp(timedev,0.0f,5.0f);
    
    float sectimebarlow = params[PAR_TimeSecondaryBarrierLow].getValue();
    sectimebarlow = clamp(sectimebarlow,1.0,64.0);
    float sectimebarhigh = params[PAR_TimeSecondaryBarrierHigh].getValue();
    sectimebarhigh = clamp(sectimebarhigh,1.0,64.0);
    sanitizeRange(sectimebarlow,sectimebarhigh,1.0f);
    
    if (m_divider.process())
    {
        for (int i=0;i<numvoices;++i)
        {
            m_oscs[i].setSampleRate(args.sampleRate);
            
            m_oscs[i].setNumSegments(numsegs);
            m_oscs[i].m_time_dev = timedev;
            m_oscs[i].m_time_mean = params[PAR_TimeMean].getValue();
            float pitch = params[PAR_CenterFrequency].getValue();
            pitch += rescale(inputs[IN_PITCH].getVoltage(i),
                -5.0f,5.0f,-60.0f,60.0f);
            pitch = clamp(pitch,-60.0f,60.0f);
            float centerfreq = dsp::FREQ_C4*pow(2.0f,1.0f/12.0f*pitch);
            m_oscs[i].setFrequencies(centerfreq,params[PAR_TimeSecondaryBarrierLow].getValue(),
                params[PAR_TimeSecondaryBarrierHigh].getValue());
            //m_oscs[i].m_time_secondary_low_barrier = sectimebarlow;
            //m_oscs[i].m_time_secondary_high_barrier = sectimebarhigh;
            float bar0 = params[PAR_TimePrimaryBarrierLow].getValue();
            float bar1 = params[PAR_TimePrimaryBarrierHigh].getValue();
            if (bar1<=bar0)
                bar1=bar0+0.01;
            m_oscs[i].m_time_primary_low_barrier = bar0;
            m_oscs[i].m_time_primary_high_barrier = bar1;
            float alux = params[PAR_AMP_BEHAVIOR].getValue();
            m_oscs[i].setAmplitudeFlux(alux);
        }
    }
    if (shouldReset == true)
    {
        for (int i=0;i<numvoices;++i)
        {
            m_oscs[i].m_ampResetMode = params[PAR_AMP_RESET_MODE].getValue();
            m_oscs[i].m_timeResetMode = params[PAR_TIME_RESET_MODE].getValue();
            float pitch = params[PAR_CenterFrequency].getValue();
            pitch += rescale(inputs[IN_PITCH].getVoltage(i),-5.0f,5.0f,-60.0f,60.0f);
            pitch = clamp(pitch,-60.0f,60.0f);
            float centerfreq = dsp::FREQ_C4*pow(2.0f,1.0f/12.0f*pitch);
            m_oscs[i].setFrequencies(centerfreq,params[PAR_TimeSecondaryBarrierLow].getValue(),
                params[PAR_TimeSecondaryBarrierHigh].getValue());
            m_oscs[i].resetTable();
        }
    }
    for (int i=0;i<numvoices;++i)
    {
        float outsample = 0.0f;
        m_oscs[i].process(&outsample,1);
        
        outputs[1].setVoltage(m_oscs[i].m_curFrequencyVolts,i);
        outputs[0].setVoltage(outsample*5.0f,i);
    }
    
    
}

GendynWidget::GendynWidget(GendynModule* m)
{
    if (!g_font)
    	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(m);
    box.size.x = RACK_GRID_WIDTH*23;
    auto port = new PortWithBackGround(m,this,GendynModule::OUT_AUDIO,1,30,"AUDIO OUT",true);
    port = new PortWithBackGround(m,this,GendynModule::OUT_PITCH,31,30,"PITCH OUT",true);
    port = new PortWithBackGround(m,this,GendynModule::IN_RESET,62,30,"RESET",false);
    float xc = 1.0f;
    float yc = 80.0f;
    addChild(new KnobInAttnWidget(this,"PITCH",GendynModule::PAR_CenterFrequency,
            GendynModule::IN_PITCH,-1,xc,yc));
    xc += 82.0f;
    addChild(new KnobInAttnWidget(this,"PITCH FLUX",GendynModule::PAR_TimeDeviation,
            -1,-1,xc,yc));
    xc += 82.0f;
    addChild(new KnobInAttnWidget(this,"PITCH MIN",GendynModule::PAR_TimeSecondaryBarrierLow,
            -1,-1,xc,yc));
    xc += 82.0f;
    addChild(new KnobInAttnWidget(this,"PITCH MAX",GendynModule::PAR_TimeSecondaryBarrierHigh,
            -1,-1,xc,yc));
    yc += 47;
    xc = 1;
    addChild(new KnobInAttnWidget(this,"AMPLITUDE FLUX",GendynModule::PAR_AMP_BEHAVIOR,
            -1,-1,xc,yc));
    xc += 82.0f;
    addChild(new KnobInAttnWidget(this,"NUM SEGMENTS",GendynModule::PAR_NUM_SEGS,
            -1,-1,xc,yc,true));
}

void GendynWidget::draw(const DrawArgs &args)
{
    nvgSave(args.vg);
    float w = box.size.x;
    float h = box.size.y;
    nvgBeginPath(args.vg);
    nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
    nvgRect(args.vg,0.0f,0.0f,w,h);
    nvgFill(args.vg);

    nvgFontSize(args.vg, 15);
    nvgFontFaceId(args.vg, g_font->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3 , 10, "GenDyn", NULL);
    nvgText(args.vg, 3 , h-11, "Xenakios", NULL);
    if (module)
    {
        GendynModule* mod = dynamic_cast<GendynModule*>(module);
        nvgText(args.vg, 1 , 20, mod->getDebugMessage().c_str(), NULL);
    }
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}

Model *modelGendynOSC = createModel<GendynModule,GendynWidget>("GendynOsc");

