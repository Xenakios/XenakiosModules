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

