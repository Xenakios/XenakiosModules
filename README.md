# XenakiosModules

VCV Rack modules. To speed up development, minimal effort has been used on the GUIs. Work-in-progress : clone, build and use only at your own risk.

## WeightedRandom

Distributes incoming gate signals up to 8 outputs with weighted random probabilities. Similar to a Bernouilli gate but has 8 outputs
instead of 2.

## Histogram

Displays incoming signal as a histogram. Mostly useful for testing random/chaotic voltage sources but could also be used for things like 
checking audio signals for DC offsets or other anomalies.

## Matrix Switcher

16 in, 16 out signal switcher.

## KeyFramer

Can store up to 32 snapshots of its 8 knob positions and recalls and interpolates between them.

## Random clock

Produces 8 unquantized, randomly timed gate signals based on exponentially distributed time intervals.

## Poly clock

Produces up to 8 gate signals based on dividing N sixteenth notes by M divisions.  

## Reduce

Reduce 8 inputs to a single output with a selectable algorithm.

- Add : Plain old mixing
- Average : Add inputs and divide by number of active inputs
- Multiply : Cumulatively multiply active inputs (like ring modulation but with more than 2 inputs)
- Minimum : Minimum of active inputs
- Maximum : Maximum of active inputs
- And : Convert inputs to 16 bit integer and perform cumulative bitwise *and*
- Or : Convert inputs to 16 bit integer and perform cumulative bitwise *or*
- Xor : Convert inputs to 16 bit integer and perform cumulative bitwise *xor*
- Difference : Cumulatively calculate absolute differences between the inputs and scale to -10.0 - 10.0 volts
- Round robin : Step through inputs at audio rate

## LOFI

Sound mangler with distortion and sample rate/bit depth reduction. Distortion types (soft clip, hard clip, foldback and wrap-around) can be smoothly crossfaded from one to another. The distortion also has an oversampled signal path and crossfades can be done between that and the non-oversampled version.

## Image Synth

**HIGHLY EXPERIMENTAL AND WORK-IN-PROGRESS**

<img src="https://github.com/Xenakios/XenakiosModules/blob/master/imgsyn01.png" width="600">

Generates audio from image files, using up to 1024 oscillators, with various tuning and other options for the oscillators.

At the moment, the image is rendered into sound as an offline process. (The module tries to keep the previously rendered sound playing, if possible, during the rendering of the new waveform.) The rendered waveform is then played back in sampler style (speed changes together with pitch) or with a rudimentary granular engine that allows adjusting speed and pitch independently. 

Although images like photographs could in principle be used directly, it's probably better to use more abstract images that have lots of black or dim pixels in them, so that all the oscillators don't sound simultaneously, which can sound like noise.

Future plans include a better granular engine and a spectral time/pitch changer as well as the ability to draw and manipulate images directly in the module. It might also be possible to make the image to sound synthesis work in real time, but with a very high CPU cost. 

