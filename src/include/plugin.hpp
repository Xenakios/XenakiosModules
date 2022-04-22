#pragma once
#include <rack.hpp>

using namespace rack;

// Declare the Plugin, defined in plugin.cpp
extern Plugin *pluginInstance;

// Declare each Model, defined in each module source file

extern Model* modelGendynOSC;
extern Model* modelPolyClock;
extern Model* modelRandomClock;
extern Model *modelXQuantizer;
extern Model* modelXPSynth;
extern Model* modelInharmonics;
extern Model* modelXStochastic;
extern Model* modelXImageSynth;
extern Model* modelXLOFI;
extern Model* modelXMultiMod;
extern Model* modelXGranular;
extern Model* modelXEnvelope;
extern Model* modelXCVShaper;
extern Model* modelXSampler;
extern Model* modelXScaleOscillator;
extern Model* modelXRandom;
//extern Model* modelXDerivator;
extern Model* modelWeightGate;
extern Model* modelHistogram;
extern Model* modelReducer;
extern Model* modelCubeSymSeq;
extern Model* modelTimeSeq;
