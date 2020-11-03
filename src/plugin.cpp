#include "plugin.hpp"
#include "weightedrandom.h"
#include "keyframer.h"
#include "clocks.h"
#include "xenutils.h"
#include "gendyn.h"
#include "audiostretcher.h"
#include <array>

Plugin *pluginInstance;

float table_sqrt[1024];

inline float distance2d(float x0, float y0, float x1, float y1)
{
	float temp0 = (x1-x0)*(x1-x0);
	float temp1 = (y1-y0)*(y1-y0);
	float temp2 = temp0+temp1;
	return std::sqrt(temp2);
}

const float pi = 3.14159265359;
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
	enum paramids
	{
		POS_X=0,
		POS_Y,
		ROTATE,
		SPREAD,
		SIZE,
		MAXCHANS,
		MASTERVOL,
		LASTPAR
	};
	std::array<std::pair<float,float>,16> m_positions;
	std::array<float,1024> m_sintable;
	float m_previous_x = 0.0f;
	float m_previous_y = 0.0f;
	float m_previous_rot = 0.0f;
	float m_previous_spread = 0.0f;
	float m_previous_size = 0.0f;
	int m_previous_max_chans = 0;
	float m_speaker_gains[16][4];
	int m_lazy_update_counter = 0;
	int m_lazy_update_interval = 44100;
	int m_samples_processed = 0;
	int m_updates_processed = 0;
	double m_update_freq = 0.0;
	MyModule()
	{
		config(LASTPAR, 16, 4, 0);
		// source orientation
		// source size
		// source aperture
		configParam(0, -1.0f, 1.0f, 0.0f, "X pos", "", 0, 1.0);
		configParam(1, -1.0f, 1.0f, 0.0f, "Y pos", "", 0, 1.0);
		configParam(2, 0.0, 360.0, 0.0, "Rotate", "Degrees", 0, 1.0);
		configParam(3, 0.0, 2.0, 0.0, "Spread", "", 0, 1.0);
		configParam(4, 0.0, 1.0, 0.0, "Size", "", 0, 1.0);
		configParam(5, 1.0, 16.0, 16.0, "Max inputs to process", "", 0, 1.0);
		configParam(6, 0.0, 2.0, 0.125, "Master volume", "%", 0, 1.0);
		int tabsize = m_sintable.size();
		for (int i=0;i<tabsize;++i)
		{
			m_sintable[i]=sinf(pi2/tabsize*i);
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
		//float spread = params[paramids::SPREAD].getValue();
		float rotphase = pi2/360.0*params[paramids::ROTATE].getValue();
		float dist_from_center = params[paramids::SIZE].getValue();
		float pos_x = params[paramids::POS_X].getValue();
		float pos_y = params[paramids::POS_Y].getValue();
		for (int i=0;i<numchans;++i)
		{
			if (!inputs[i].isConnected())
				continue;
			float sinphase = pi2/numchans*i+rotphase;
			float cosphase = pi/2.0-sinphase;
			int tabindex = (int)(m_sintable.size()/pi2*cosphase) & 1023;
			m_positions[i].first=pos_x + dist_from_center*m_sintable[tabindex];
			tabindex = (int)(m_sintable.size()/pi2*sinphase) & 1023;
			m_positions[i].second=pos_y + dist_from_center*m_sintable[tabindex];
			m_positions[i].first = clamp(m_positions[i].first,-1.0f,1.0f);
			m_positions[i].second = clamp(m_positions[i].second,-1.0f,1.0f);
		}
	}
	bool areParametersDirty()
	{
		return params[paramids::POS_X].getValue()!=m_previous_x
		|| params[paramids::POS_Y].getValue()!=m_previous_y
		|| params[paramids::SPREAD].getValue()!=m_previous_spread
		|| params[paramids::SIZE].getValue()!=m_previous_size
		|| params[paramids::ROTATE].getValue()!=m_previous_rot
		|| (int)params[paramids::MAXCHANS].getValue()!=m_previous_max_chans;
	}
	void process(const ProcessArgs& args) override
	{
		for (int i=0;i<4;++i)
			outputs[i].setVoltage(0.0f);
		++m_samples_processed;
		++m_lazy_update_counter;
		int maxinchans = params[paramids::MAXCHANS].getValue();
		if (areParametersDirty() || m_lazy_update_counter>=m_lazy_update_interval)
		{
			updatePositions(maxinchans);
			updateSpeakerGains(maxinchans);
			m_previous_x = params[paramids::POS_X].getValue();
			m_previous_y = params[paramids::POS_Y].getValue();
			m_previous_size = params[paramids::SIZE].getValue();
			m_previous_rot = params[paramids::ROTATE].getValue();
			m_previous_spread = params[paramids::SPREAD].getValue();
			m_previous_max_chans = maxinchans;
			m_lazy_update_counter = 0;
			++m_updates_processed;
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
		float mastergain = params[paramids::MASTERVOL].getValue();
		for (int i=0;i<4;++i)
		{
			outputs[i].setVoltage(mastergain*outputs[i].getVoltage());
		}
		m_update_freq = (float)m_samples_processed/m_updates_processed;
	}
};

std::shared_ptr<Font> g_font;

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
		int numchans = m_mod->params[MyModule::MAXCHANS].getValue();
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
			
			nvgFontSize(args.vg, 13);
			nvgFontFaceId(args.vg, g_font->handle);
			nvgTextLetterSpacing(args.vg, -2);
			nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
			char buf[10];
			sprintf(buf,"%d",i+1);
			nvgText(args.vg, xcor , ycor+15.0f , buf, NULL);
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
		if (!g_font)
			g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
		setModule(module);
		box.size.x = 500;
		m_spatWidget = new SpatWidget(module);
		m_spatWidget->box.pos = Vec(5,90);
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
		addParam(createParam<RoundHugeBlackKnob>(Vec(3, 30), module, MyModule::POS_X));
		addParam(createParam<RoundHugeBlackKnob>(Vec(63, 30), module, MyModule::POS_Y));
		addParam(createParam<RoundHugeBlackKnob>(Vec(123, 30), module, MyModule::ROTATE));
		addParam(createParam<RoundHugeBlackKnob>(Vec(183, 30), module, MyModule::SPREAD));
		addParam(createParam<RoundHugeBlackKnob>(Vec(243, 30), module, MyModule::SIZE));
		addParam(createParam<RoundHugeBlackKnob>(Vec(303, 30), module, MyModule::MAXCHANS));
		addParam(createParam<RoundHugeBlackKnob>(Vec(363, 30), module, MyModule::MASTERVOL));
	}
	void draw(const DrawArgs &args) override
	{
		
		nvgSave(args.vg);
		
		float w = box.size.x;
		float h = box.size.y;
		nvgBeginPath(args.vg);
		nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
		nvgRect(args.vg,0.0f,0.0f,w,h);
		nvgFill(args.vg);
		
		auto mod = dynamic_cast<MyModule*>(this->module);
		if (mod && g_font)
		{
			nvgFontSize(args.vg, 13);
			nvgFontFaceId(args.vg, g_font->handle);
			nvgTextLetterSpacing(args.vg, -2);
			nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
			char buf[200];
			sprintf(buf,"SPATIALIZER (%d param updates)",mod->m_updates_processed);
			nvgText(args.vg, 3 , 10, buf, NULL);
		}
		nvgRestore(args.vg);
		ModuleWidget::draw(args);
	}
};



void init(Plugin *p) {
	pluginInstance = p;
	//p->addModel(createModel<MyModule,MyModuleWidget>("Spatializer"));
	p->addModel(createModel<WeightedRandomModule,WeightedRandomWidget>("WeightedRandom"));
	p->addModel(createModel<HistogramModule,HistogramModuleWidget>("Histogram"));
	p->addModel(createModel<MatrixSwitchModule,MatrixSwitchWidget>("MatrixSwitcher"));
	p->addModel(createModel<KeyFramerModule,KeyFramerWidget>("XKeyFramer"));
	p->addModel(createModel<RandomClockModule,RandomClockWidget>("RandomClock"));
	p->addModel(createModel<DivisionClockModule,DividerClockWidget>("DividerClock"));
	p->addModel(createModel<ReducerModule,ReducerWidget>("Reduce"));
	p->addModel(createModel<DecahexCVTransformer,DecahexCVTransformerWidget>("DecahexCVTransformer"));
	p->addModel(createModel<GendynModule,GendynWidget>("GendynOsc"));
	p->addModel(createModel<DerivatorModule,DerivatorWidget>("Derivator"));
#ifdef RBMODULE
	p->addModel(createModel<AudioStretchModule,AudioStretchWidget>("XAudioStretch"));
#endif
	p->addModel(modelXQuantizer);
	p->addModel(modelXPSynth);
	p->addModel(modelRandom);
	p->addModel(modelInharmonics);
	p->addModel(modelXStochastic);
	p->addModel(modelXImageSynth);
	p->addModel(modelXLOFI);
	p->addModel(modelXMultiMod);
}
