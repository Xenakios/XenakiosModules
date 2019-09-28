#include "plugin.hpp"


Plugin *pluginInstance;

inline float distance2d(float x0, float y0, float x1, float y1)
{
	float temp0 = (x1-x0)*(x1-x0);
	float temp1 = (y1-y0)*(y1-y0);
	return sqrt(temp0+temp1);
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
	float m_speaker_gains[16][4];
	MyModule()
	{
		config(2, 16, 4, 0);
		configParam(0, 0.0, 360.0, 0.0, "Rotate", "Degrees", 0, 1.0);
		configParam(1, 0.0, 1.0, 0.0, "Spread", "Degrees", 0, 1.0);
		int tabsize = m_sincostable.size();
		for (int i=0;i<tabsize;++i)
		{
			m_sincostable[i].first=cos(pi2/tabsize*i);
			m_sincostable[i].second=sin(pi2/tabsize*i);
		}
		updatePositions();
		updateSpeakerGains();
	}
	void updateSpeakerGains()
	{
		for (int i=0;i<16;++i)
		{
			float input_x = m_positions[i].first;
			float input_y = m_positions[i].second;

			for (int j=0;j<4;++j)
			{
				float distance = 0.5*distance2d(input_x,input_y,speaker_positions[j][0],speaker_positions[j][1]);
				float gain = myclamp(1.0f-distance,0.0f,1.0f);
				m_speaker_gains[i][j]=gain;
			}
		}
	}
	void updatePositions()
	{
		float rotphase = pi2/360.0*params[0].getValue();
		float dist_from_center = params[1].getValue();
		for (int i=0;i<16;++i)
		{
			float phase = pi2/15*i+rotphase;
			int tabindex = (int)(m_sincostable.size()/pi2*phase) & 1023;
			m_positions[i].first=dist_from_center*m_sincostable[tabindex].first;
			m_positions[i].second=dist_from_center*m_sincostable[tabindex].second;
			//sincosf(pi2/15*i+rotphase,&m_positions[i].first,&m_positions[i].second);
			//float input_x = dist_from_center * cos(pi2/15*i+rotphase);
			//float input_y = dist_from_center * sin(pi2/15*i+rotphase);
			//multippair(m_positions[i],dist_from_center);
		}
	}
	void process(const ProcessArgs& args) override
	{
		
		for (int i=0;i<4;++i)
			outputs[i].setVoltage(0.0f);
		
		if (params[0].getValue()!=m_previous_rot || params[1].getValue()!=m_previous_spread)
		{
			updatePositions();
			updateSpeakerGains();
			m_previous_rot = params[0].getValue();
			m_previous_spread = params[1].getValue();
		}
		for (int i=0;i<16;++i)
		{
			//float input_x = m_positions[i].first;
			//float input_y = m_positions[i].second;

			for (int j=0;j<4;++j)
			{
				//float distance = 0.5*distance2d(input_x,input_y,speaker_positions[j][0],speaker_positions[j][1]);
				//float gain = myclamp(1.0f-distance,0.0f,1.0f);
				float gain = m_speaker_gains[i][j];
				float outv = outputs[j].getVoltage()+0.1*gain*inputs[i].getVoltage();
				outputs[j].setVoltage(outv);
			}
		}
	}
};

class MyModuleWidget : public ModuleWidget
{
public:
	MyModuleWidget(MyModule* module)
	{
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/drawing.svg")));
		for (int i=0;i<16;++i)
		{
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.099+10.0*i, 96.025)), module, i));
		}
		for (int i=0;i<4;++i)
		{
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.099+10.0*i, 106.025)), module, i));
		}
		addParam(createParam<RoundHugeBlackKnob>(Vec(3, 3), module, 0));
		addParam(createParam<RoundHugeBlackKnob>(Vec(60, 3), module, 1));
	}
};

void init(Plugin *p) {
	pluginInstance = p;
	std::cout << "yhhyh" << "\n";
	// Add modules here
	p->addModel(createModel<MyModule,MyModuleWidget>("Spatializer"));
	
	// Any other plugin initialization may go here.
	// As an alternative, consider lazy-loading assets and lookup tables when your module is created to reduce startup times of Rack.
}
