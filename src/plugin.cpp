#include "plugin.hpp"


Plugin *pluginInstance;

float table_sqrt[1024];

void init_table_sqrt()
{
	for (int i=0;i<1024;++i)
	{
		float x = -10.0+20.0/1024*i;
		table_sqrt[i]=std::sqrt(x);
	}
}

inline float distance2d(float x0, float y0, float x1, float y1)
{
	float temp0 = (x1-x0)*(x1-x0);
	float temp1 = (y1-y0)*(y1-y0);
	float temp2 = temp0+temp1;
	return std::sqrt(temp2);
}

inline float myclamp(float val, float minval, float maxval)
{
	if (val<minval)
		return minval;
	if (val>maxval)
		return maxval;
	return val;
}

const float pi = 3.141592654;
const float pi2 = pi*2.0f;

const float speaker_positions[4][2]={
			{-1.0f,-1.0f},
			{1.0f,-1.0f},
			{-1.0f,1.0f},
			{1.0f,1.0f}};

inline void multippair(std::pair<float,float>& p, float multip)
{
	p.first*=multip;
	p.second*=multip;
}

class MyModule : public rack::Module
{
public:
	std::array<std::pair<float,float>,16> m_positions;
	std::array<std::pair<float,float>,1024> m_sincostable;
	float m_previous_rot = 0.0f;
	float m_previous_spread = 0.0;
	int m_previous_max_chans = 0;
	float m_speaker_gains[16][4];
	int m_lazy_update_counter = 0;
	int m_lazy_update_interval = 44100;
	MyModule()
	{
		config(4, 16, 4, 0);
		configParam(0, 0.0, 360.0, 0.0, "Rotate", "Degrees", 0, 1.0);
		configParam(1, 0.0, 1.0, 0.0, "Spread", "Degrees", 0, 1.0);
		configParam(2, 1.0, 16.0, 16.0, "Max inputs to process", "", 0, 1.0);
		configParam(3, 0.0, 2.0, 0.125, "Master volume", "%", 0, 1.0);
		int tabsize = m_sincostable.size();
		for (int i=0;i<tabsize;++i)
		{
			m_sincostable[i].first=cos(pi2/tabsize*i);
			m_sincostable[i].second=sin(pi2/tabsize*i);
		}
		updatePositions(16);
		updateSpeakerGains(16);
	}
	void updateSpeakerGains(int numchans)
	{
		for (int i=0;i<numchans;++i)
		{
			if (!inputs[i].isConnected())
				continue;
			float input_x = m_positions[i].first;
			float input_y = m_positions[i].second;

			for (int j=0;j<4;++j)
			{
				float distance = 0.5*distance2d(input_x,input_y,speaker_positions[j][0],speaker_positions[j][1]);
				float gain = clamp(1.0f-distance,0.0f,1.0f);
				m_speaker_gains[i][j]=gain;
			}
		}
	}
	void updatePositions(int numchans)
	{
		float rotphase = pi2/360.0*params[0].getValue();
		float dist_from_center = params[1].getValue();
		for (int i=0;i<numchans;++i)
		{
			if (!inputs[i].isConnected())
				continue;
			float phase = pi2/numchans*i+rotphase;
			int tabindex = (int)(m_sincostable.size()/pi2*phase) & 1023;
			m_positions[i].first=dist_from_center*m_sincostable[tabindex].first;
			m_positions[i].second=dist_from_center*m_sincostable[tabindex].second;
		}
	}
	void process(const ProcessArgs& args) override
	{
		for (int i=0;i<4;++i)
			outputs[i].setVoltage(0.0f);
		++m_lazy_update_counter;
		int maxinchans = params[2].getValue();
		if (params[0].getValue()!=m_previous_rot 
			|| params[1].getValue()!=m_previous_spread
			|| maxinchans!=m_previous_max_chans
			|| m_lazy_update_counter>=m_lazy_update_interval)
		{
			updatePositions(maxinchans);
			updateSpeakerGains(maxinchans);
			m_previous_rot = params[0].getValue();
			m_previous_spread = params[1].getValue();
			m_previous_max_chans = maxinchans;
			m_lazy_update_counter = 0;
		}
		for (int i=0;i<maxinchans;++i)
		{
			if (!inputs[i].isConnected())
				continue;
			for (int j=0;j<4;++j)
			{
				float gain = m_speaker_gains[i][j];
				float outv = outputs[j].getVoltage()+gain*inputs[i].getVoltage();
				outputs[j].setVoltage(outv);
			}
		}
		float mastergain = params[3].getValue();
		for (int i=0;i<4;++i)
		{
			outputs[i].setVoltage(mastergain*outputs[i].getVoltage());
		}
	}
};

class SpatWidget : public TransparentWidget
{
public:
	SpatWidget(MyModule* m) : m_mod(m) 
	{}
	void draw(const DrawArgs &args) override
	{
		if (m_mod == nullptr)
			return;
		nvgSave(args.vg);
		
		float w = box.size.x;
		float h = box.size.y;
		nvgBeginPath(args.vg);
		nvgFillColor(args.vg, nvgRGBA(0x00, 0x00, 0x00, 0xff));
		nvgRect(args.vg,0.0f,0.0f,w,h);
		nvgFill(args.vg);
		int numchans = m_mod->params[2].getValue();
		for (int i=0;i<numchans;++i)
		{
			if (!m_mod->inputs[i].isConnected())
				continue;
			float x = m_mod->m_positions[i].first;
			float y = m_mod->m_positions[i].second;
			float xcor = rescale(x,-1.0f,1.0f,0.0f,w); //w/2.0*(x+1.0);
			float ycor = rescale(y,-1.0f,1.0f,0.0f,h); // h/2.0*(y+1.0);
			nvgBeginPath(args.vg);
			nvgFillColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0xff));
			nvgCircle(args.vg,xcor,ycor,5.0f);
			nvgFill(args.vg);
		}
		nvgRestore(args.vg);
	}
private:
	MyModule* m_mod = nullptr;
};

class MyModuleWidget : public ModuleWidget
{
public:
	SpatWidget* m_spatWidget = nullptr;	
	MyModuleWidget(MyModule* module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/drawing.svg")));
		m_spatWidget = new SpatWidget(module);
		m_spatWidget->box.pos = Vec(5,75);
		m_spatWidget->box.size = Vec(150,150);
		addChild(m_spatWidget);
		for (int i=0;i<16;++i)
		{
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.099+10.0*i, 96.025)), module, i));
		}
		for (int i=0;i<4;++i)
		{
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.099+10.0*i, 106.025)), module, i));
		}
		addParam(createParam<RoundHugeBlackKnob>(Vec(3, 3), module, 0));
		addParam(createParam<RoundHugeBlackKnob>(Vec(63, 3), module, 1));
		addParam(createParam<RoundHugeBlackKnob>(Vec(123, 3), module, 2));
		addParam(createParam<RoundHugeBlackKnob>(Vec(183, 3), module, 3));
				
	}
};

void juuh()
{
	//rack::dsp::SchmittTrigger trig;
	//rescale()
}

void init(Plugin *p) {
	init_table_sqrt();
	pluginInstance = p;
	p->addModel(createModel<MyModule,MyModuleWidget>("Spatializer"));
}
