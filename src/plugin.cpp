#include "plugin.hpp"
#include "old/weightedrandom.h"
#include "old/xenutils.h"
#include "old/audiostretcher.h"
#include <array>

Plugin *pluginInstance;

std::shared_ptr<rack::Font> getDefaultFont(int which)
{
	if (which == 0)
		return APP->window->loadFont(asset::plugin(pluginInstance, "res/Nunito-Bold.ttf"));
	return APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
}

void init(Plugin *p) {
	pluginInstance = p;
	//p->addModel(createModel<MyModule,MyModuleWidget>("Spatializer"));
	p->addModel(modelWeightGate);
	p->addModel(modelHistogram);
	
	
	p->addModel(modelRandomClock);
	p->addModel(modelPolyClock);
	p->addModel(modelReducer);
	
	p->addModel(modelGendynOSC);
	p->addModel(modelXRandom);
	p->addModel(modelXDerivator);
#ifdef RBMODULE
	p->addModel(createModel<AudioStretchModule,AudioStretchWidget>("XAudioStretch"));
#endif
	p->addModel(modelXQuantizer);
	p->addModel(modelXPSynth);
	p->addModel(modelInharmonics);
	p->addModel(modelXStochastic);
	p->addModel(modelXImageSynth);
	p->addModel(modelXLOFI);
	p->addModel(modelXMultiMod);
	p->addModel(modelXGranular);
	//p->addModel(modelXEnvelope);
	p->addModel(modelXCVShaper);
	p->addModel(modelXSampler);
	p->addModel(modelXScaleOscillator);
	p->addModel(modelCubeSymSeq);
	p->addModel(modelTimeSeq);
}
