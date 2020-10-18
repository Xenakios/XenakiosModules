#pragma once
#include <rack.hpp>

using namespace rack;

inline float customlog(float base, float x)
{
	return std::log(x)/std::log(base);
}


// Declare the Plugin, defined in plugin.cpp
extern Plugin *pluginInstance;

// Declare each Model, defined in each module source file
extern Model *modelRandom;

extern Model *modelXQuantizer;
extern Model* modelXPSynth;
extern Model* modelInharmonics;
extern Model* modelXStochastic;
extern Model* modelXImageSynth;

template <typename TLightBase = RedLight>
struct LEDLightSliderFixed : LEDLightSlider<TLightBase> {
	LEDLightSliderFixed() {
		this->setHandleSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LEDSliderHandle.svg")));
	}
};
