#include "plugin.hpp"
#include <array>

struct Random : Module {
	enum ParamIds {
		RATE_PARAM,
		SHAPE_PARAM,
		OFFSET_PARAM,
		MODE_PARAM,
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
		STEPPED_OUTPUT,
		LINEAR_OUTPUT,
		SMOOTH_OUTPUT,
		EXPONENTIAL_OUTPUT,
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
	std::array<int,16> lastTrigFrames{INT_MAX};

	Random() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(RATE_PARAM, std::log2(0.002f), std::log2(2000.f), std::log2(2.f), "Rate", " Hz", 2);
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.5f, "Shape", "%", 0, 100);
		configParam(OFFSET_PARAM, 0.f, 1.f, 1.f, "Bipolar/unipolar");
		configParam(MODE_PARAM, 0.f, 1.f, 1.f, "Relative/absolute randomness");
	}

	void trigger(int numchans) {
		lastValues = values;
		for (int polychan = 0 ; polychan < numchans; ++polychan)
		{
		if (inputs[EXTERNAL_INPUT].isConnected()) {
			values[polychan] = inputs[EXTERNAL_INPUT].getVoltage() / 10.f;
		}
		else {
			// Choose a new random value
			bool absolute = params[MODE_PARAM].getValue() > 0.f;
			bool uni = params[OFFSET_PARAM].getValue() > 0.f;
			if (absolute) {
				values[polychan] = random::uniform();
				if (!uni)
					values[polychan] -= 0.5f;
			}
			else {
				// Switch to uni if bi
				if (!uni)
					values[polychan] += 0.5f;
				float deltaValue = random::normal();
				// Bias based on value
				deltaValue -= (values[polychan] - 0.5f) * 2.f;
				// Scale delta and accumulate value
				const float stdDev = 1 / 10.f;
				deltaValue *= stdDev;
				values[polychan] += deltaValue;
				values[polychan] = clamp(values[polychan], 0.f, 1.f);
				// Switch back to bi
				if (!uni)
					values[polychan] -= 0.5f;
			}
		}
		}
		lights[RATE_LIGHT].setBrightness(3.f);
	}

	void process(const ProcessArgs& args) override {
		int numpolychans = 2;
		if (inputs[TRIGGER_INPUT].isConnected()) {
			for (int polychan = 0 ; polychan < numpolychans; ++polychan)
			{
			// Advance clock phase based on tempo estimate
			trigFrame[polychan]++;
			float deltaPhase = 1.f / lastTrigFrames[polychan];
			clockPhases[polychan] += deltaPhase;
			clockPhases[polychan] = std::min(clockPhases[polychan], 1.f);
			// Trigger
			if (trigTriggers[polychan].process(rescale(inputs[TRIGGER_INPUT].getVoltage(), 0.1f, 2.f, 0.f, 1.f))) {
				clockPhases[polychan] = 0.f;
				lastTrigFrames[polychan] = trigFrame[polychan];
				trigFrame[polychan] = 0;
				trigger(numpolychans);
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
				trigger(numpolychans);
			}
			}
		}
		outputs[LINEAR_OUTPUT].setChannels(numpolychans);
		outputs[STEPPED_OUTPUT].setChannels(numpolychans);
		outputs[SMOOTH_OUTPUT].setChannels(numpolychans);
		outputs[EXPONENTIAL_OUTPUT].setChannels(numpolychans);
		for (int polychan = 0 ; polychan < numpolychans ; ++polychan)
		{
		// Shape
		float shape = params[SHAPE_PARAM].getValue();
		shape += inputs[SHAPE_INPUT].getVoltage() / 10.f;
		shape = clamp(shape, 0.f, 1.f);

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
			// Lights
		lights[RATE_LIGHT].setSmoothBrightness(0.f, args.sampleTime);
		lights[SHAPE_LIGHT].setBrightness(shape);
		}
		
	}
};


struct RandomWidget : ModuleWidget {
	RandomWidget(Random* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Random.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createLightParamCentered<LEDLightSliderFixed<GreenLight>>(mm2px(Vec(7.215, 30.858)), module, Random::RATE_PARAM, Random::RATE_LIGHT));
		addParam(createLightParamCentered<LEDLightSliderFixed<GreenLight>>(mm2px(Vec(18.214, 30.858)), module, Random::SHAPE_PARAM, Random::SHAPE_LIGHT));
		addParam(createParamCentered<CKSS>(mm2px(Vec(7.214, 78.259)), module, Random::OFFSET_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(18.214, 78.259)), module, Random::MODE_PARAM));

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
