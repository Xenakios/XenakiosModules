#include "plugin.hpp"
#include "old/weightedrandom.h"
#include "old/keyframer.h"
#include "xenutils.h"
#include "old/audiostretcher.h"
#include <array>

Plugin *pluginInstance;

std::shared_ptr<rack::Font> getDefaultFont(int which)
{
	static std::map<int,std::shared_ptr<rack::Font>> s_fonts;
	if (which<0 || which>1)
		which = 0;
	if (s_fonts.count(which)==0)
	{
		std::shared_ptr<rack::Font> font;
		if (which == 0)
			font = APP->window->loadFont(asset::plugin(pluginInstance, "res/Nunito-Bold.ttf"));
		else if (which == 1)
			font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/sudo.ttf"));
		s_fonts[which] = font;
	}
	return s_fonts[which];
}


void init(Plugin *p) {
	pluginInstance = p;
	//p->addModel(createModel<MyModule,MyModuleWidget>("Spatializer"));
	p->addModel(createModel<WeightedRandomModule,WeightedRandomWidget>("WeightedRandom"));
	p->addModel(createModel<HistogramModule,HistogramModuleWidget>("Histogram"));
	p->addModel(createModel<MatrixSwitchModule,MatrixSwitchWidget>("MatrixSwitcher"));
	p->addModel(createModel<KeyFramerModule,KeyFramerWidget>("XKeyFramer"));
	p->addModel(modelRandomClock);
	p->addModel(modelPolyClock);
	p->addModel(createModel<ReducerModule,ReducerWidget>("Reduce"));
	p->addModel(createModel<DecahexCVTransformer,DecahexCVTransformerWidget>("DecahexCVTransformer"));
	p->addModel(modelGendynOSC);
	p->addModel(modelXRandom);
	p->addModel(createModel<DerivatorModule,DerivatorWidget>("Derivator"));
#ifdef RBMODULE
	p->addModel(createModel<AudioStretchModule,AudioStretchWidget>("XAudioStretch"));
#endif
	p->addModel(modelXQuantizer);
	p->addModel(modelXPSynth);
	//p->addModel(modelRandom);
	p->addModel(modelInharmonics);
	p->addModel(modelXStochastic);
	p->addModel(modelXImageSynth);
	p->addModel(modelXLOFI);
	p->addModel(modelXMultiMod);
	p->addModel(modelXGranular);
	p->addModel(modelXEnvelope);
	p->addModel(modelXCVShaper);
	p->addModel(modelXSampler);
	p->addModel(modelXScaleOscillator);
}
