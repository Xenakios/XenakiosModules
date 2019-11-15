# XenakiosModules

VCV Rack modules. To speed up development, minimal effort has been used on the GUIs.

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
- Multiply : Start with 1.0 and cumulatively multiply active inputs
- Minimum : Minimum of active inputs
- Maximum : Maximum of active inputs
- Round robin : Step through inputs
