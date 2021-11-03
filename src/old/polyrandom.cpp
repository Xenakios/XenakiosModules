#include "../plugin.hpp"
#include <array>
#include <random>
#include <algorithm>

struct Random : Module {
	enum ParamIds {
		PAR_MASTER_RATE,
		ENUMS(PAR_RATE_MULTIP, 8),
		ENUMS(PAR_SHAPEMODE, 8),
		ENUMS(PAR_SHAPE, 8),
		ENUMS(PAR_OUTRANGE, 8),
		ENUMS(PAR_RANDDIST, 8),
		ENUMS(PAR_RANDDISTCENTER, 8),
		ENUMS(PAR_RANDDISTSPREAD, 8),
		ENUMS(PAR_RANDLIMITMODE, 8),
		ENUMS(PAR_RANDMODE, 8), // direct or random walk
		NUM_PARAMS
	};
	enum InputIds {
		RATE_INPUT,
		SHAPE_INPUT,
		TRIGGER_INPUT,
		EXTERNAL_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUTPUT, 8),
		
		NUM_OUTPUTS
	};
	enum LightIds {
		RATE_LIGHT,
		SHAPE_LIGHT,
		NUM_LIGHTS
	};

	std::array<dsp::SchmittTrigger,16> trigTriggers;
	std::array<float,16> lastValues{};
	std::array<float,16> values{};
	std::array<float,16> clockPhases{};
	std::array<int,16> trigFrame{};
	std::array<int,16> lastTrigFrames; //{{INT_MAX}}; // does this really init all the array elements...?

	int curnumoutchans = 0;

	dsp::ClockDivider chancountdiv;
	std::minstd_rand randgen;
	std::uniform_real_distribution<float> randdist_uni{0.0f,1.0f};
	std::cauchy_distribution<float> randdist_cauchy{0.0,1.0f};
	Random() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PAR_MASTER_RATE, std::log2(0.002f), std::log2(2000.f), std::log2(2.f), "Rate", " Hz", 2);
		for (int i=0;i<8;++i)
		{
			configParam(PAR_RATE_MULTIP+i, 0.01f, 10.f, 1.f, "Rate multiplier");
			configParam(PAR_SHAPE+i, 0.f, 1.f, 0.5f, "Shape", "%", 0, 100);
			configParam(PAR_OUTRANGE+i, 0.f, 1.f, 1.f, "Bipolar/unipolar");
			configParam(PAR_RANDMODE+i, 0.f, 1.f, 1.f, "Relative/absolute randomness");	
			configParam(PAR_SHAPEMODE+i, 0.f, 3.f, 0.0f, "Shape (step/linear/smooth/exp)");
			configParam(PAR_RANDDIST+i, 0.f, 2.f, 0.0f, "Random distribution (uniform/normal/Cauchy)");
			configParam(PAR_RANDLIMITMODE+i, 0.f, 3.f, 1.0f, "Random limit mode (try again/clip/reflect/wrap)");
			configParam(PAR_RANDDISTCENTER+i, -1.f, 1.f, 0.0f, "Random distribution center");
			configParam(PAR_RANDDISTSPREAD+i, 0.f, 1.f, 0.2f, "Random distribution spread");

		}
		chancountdiv.setDivision(512);
		std::fill(lastTrigFrames.begin(),lastTrigFrames.end(),INT_MAX);
	}

	void trigger(int numchan) {
		lastValues[numchan] = values[numchan];
		if (inputs[EXTERNAL_INPUT].isConnected()) {
			values[numchan] = inputs[EXTERNAL_INPUT].getVoltage() / 10.f;
		}
		else {
			// Choose a new random value
			bool absolute = params[MODE_PARAM].getValue() > 0.f;
			bool uni = params[OFFSET_PARAM].getValue() > 0.f;
			if (absolute) {
				//values[numchan] = random::uniform();
				values[numchan] = randdist_uni(randgen);
				if (!uni)
					values[numchan] -= 0.5f;
			}
			else {
				// Switch to uni if bi
				if (!uni)
					values[numchan] += 0.5f;
				float deltaValue = random::normal();
				// Bias based on value
				deltaValue -= (values[numchan] - 0.5f) * 2.f;
				// Scale delta and accumulate value
				const float stdDev = 1 / 10.f;
				deltaValue *= stdDev;
				values[numchan] += deltaValue;
				values[numchan] = clamp(values[numchan], 0.f, 1.f);
				// Switch back to bi
				if (!uni)
					values[numchan] -= 0.5f;
			}
		}
		if (numchan == 0)
			lights[RATE_LIGHT].setBrightness(3.f);
	}

	void process(const ProcessArgs& args) override {
		int numpolychans = params[NUMVOICES_PARAM].getValue();
		if (inputs[TRIGGER_INPUT].isConnected()) {
			int trigchans = inputs[TRIGGER_INPUT].getChannels();
			if (trigchans>1)
				numpolychans = trigchans;
			for (int polychan = 0 ; polychan < numpolychans; ++polychan)
			{
			// Advance clock phase based on tempo estimate
			trigFrame[polychan]++;
			float deltaPhase = 1.f / lastTrigFrames[polychan];
			clockPhases[polychan] += deltaPhase;
			clockPhases[polychan] = std::min(clockPhases[polychan], 1.f);
			// Trigger
			if (trigTriggers[polychan].process(
					rescale(inputs[TRIGGER_INPUT].getVoltage(polychan), 0.1f, 2.f, 0.f, 1.f))) {
				clockPhases[polychan] = 0.f;
				lastTrigFrames[polychan] = trigFrame[polychan];
				trigFrame[polychan] = 0;
				trigger(polychan);
			}
			}
		}
		else {
			// Advance clock phase by rate
			for (int polychan = 0 ; polychan < numpolychans ; ++polychan)
			{
				float rate = params[RATE_PARAM].getValue();
				rate += inputs[RATE_PARAM].getVoltage();
				float clockFreq = std::pow(2.f, rate);
				float deltaPhase = std::fmin(clockFreq * args.sampleTime, 0.5f);
				clockPhases[polychan] += deltaPhase;
				// Trigger
				if (clockPhases[polychan] >= 1.f) {
					clockPhases[polychan] -= 1.f;
					trigger(polychan);
				}
			}
		}
		if (chancountdiv.process())
		{
			outputs[LINEAR_OUTPUT].setChannels(numpolychans);
			outputs[STEPPED_OUTPUT].setChannels(numpolychans);
			outputs[SMOOTH_OUTPUT].setChannels(numpolychans);
			outputs[EXPONENTIAL_OUTPUT].setChannels(numpolychans);
			curnumoutchans = numpolychans;
		}
		float shape = 0.0f;
		// Shape
		shape = params[SHAPE_PARAM].getValue();
		shape += inputs[SHAPE_INPUT].getVoltage() / 10.f;
		shape = clamp(shape, 0.f, 1.f);
		for (int polychan = 0 ; polychan < numpolychans ; ++polychan)
		{
		

		// Stepped
		if (outputs[STEPPED_OUTPUT].isConnected()) {
			float steps = std::ceil(std::pow(shape, 2) * 15 + 1);
			float v = std::ceil(clockPhases[polychan] * steps) / steps;
			v = rescale(v, 0.f, 1.f, lastValues[polychan], values[polychan]);
			outputs[STEPPED_OUTPUT].setVoltage(v * 10.f,polychan);
		}

		// Linear
		if (outputs[LINEAR_OUTPUT].isConnected()) {
			float slope = 1 / shape;
			float v;
			if (slope < 1e6f) {
				v = std::fmin(clockPhases[polychan] * slope, 1.f);
			}
			else {
				v = 1.f;
			}
			v = rescale(v, 0.f, 1.f, lastValues[polychan], values[polychan]);
			
			outputs[LINEAR_OUTPUT].setVoltage(v * 10.f,polychan);
		}

		// Smooth
		if (outputs[SMOOTH_OUTPUT].isConnected()) {
			float p = 1 / shape;
			float v;
			if (p < 1e6f) {
				v = std::fmin(clockPhases[polychan] * p, 1.f);
				v = std::cos(M_PI * v);
			}
			else {
				v = -1.f;
			}
			v = rescale(v, 1.f, -1.f, lastValues[polychan], values[polychan]);
			outputs[SMOOTH_OUTPUT].setVoltage(v * 10.f,polychan);
		}

		// Exp
		if (outputs[EXPONENTIAL_OUTPUT].isConnected()) {
			float b = std::pow(shape, 4);
			float v;
			if (0.999f < b) {
				v = clockPhases[polychan];
			}
			else if (1e-20f < b) {
				v = (std::pow(b, clockPhases[polychan]) - 1.f) / (b - 1.f);
			}
			else {
				v = 1.f;
			}
			v = rescale(v, 0.f, 1.f, lastValues[polychan], values[polychan]);
			outputs[EXPONENTIAL_OUTPUT].setVoltage(v * 10.f,polychan);
		}
			
		}
		// Lights
		lights[RATE_LIGHT].setSmoothBrightness(0.f, args.sampleTime);
		lights[SHAPE_LIGHT].setBrightness(shape);
	}
};


struct RandomWidget : ModuleWidget {
	RandomWidget(Random* module) {
		setModule(module);
		//setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Random.svg")));
		box.size.x = 15*20;
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createLightParamCentered<LEDLightSliderFixed<GreenLight>>(mm2px(Vec(7.215, 30.858)), module, Random::RATE_PARAM, Random::RATE_LIGHT));
		addParam(createLightParamCentered<LEDLightSliderFixed<GreenLight>>(mm2px(Vec(18.214, 30.858)), module, Random::SHAPE_PARAM, Random::SHAPE_LIGHT));
		addParam(createParamCentered<CKSS>(mm2px(Vec(7.214, 78.259)), module, Random::OFFSET_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(18.214, 78.259)), module, Random::MODE_PARAM));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(5.0, 121.0)), module, Random::NUMVOICES_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.214, 50.726)), module, Random::RATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(18.214, 50.726)), module, Random::SHAPE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.214, 64.513)), module, Random::TRIGGER_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(18.214, 64.513)), module, Random::EXTERNAL_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.214, 96.727)), module, Random::STEPPED_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(18.214, 96.727)), module, Random::LINEAR_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.214, 112.182)), module, Random::SMOOTH_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(18.214, 112.182)), module, Random::EXPONENTIAL_OUTPUT));
	}
};


Model* modelRandom = createModel<Random, RandomWidget>("XPolyRandom");
