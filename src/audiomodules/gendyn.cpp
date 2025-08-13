#include <rack.hpp>
#include "plugin.hpp"
#include <functional>
#include <atomic>
#include <random>
#include "helperwidgets.h"

/* Remaps a value from a source range to a target range. Explodes if source range has zero size.
 */
template <typename Type>
inline Type mapvalue(Type sourceValue, Type sourceRangeMin, Type sourceRangeMax,
                     Type targetRangeMin, Type targetRangeMax)
{
    return targetRangeMin + ((targetRangeMax - targetRangeMin) * (sourceValue - sourceRangeMin)) /
                                (sourceRangeMax - sourceRangeMin);
}

/*
 The C++ standard library random stuff can be a bit bonkers(*) at times,
 so we have this custom class which has a decent enough random base generator
 and some simple methods for getting values out as floats etc...

(*) See for example the Microsoft implementation of std::uniform_int_distribution...
Or the Cauchy distribution, which won't allow a scale factor of 0 to be used, while
useful for our audio/music applications as a special case.
*/

struct Xoroshiro128Plus
{
    // have some non-zero init state to avoid the zero init state problem
    // which would cause only zeros to be produced
    uint64_t state[2] = {4294967311, 100007};
    Xoroshiro128Plus()
    {
        // experimentally known that after seeding, useful to advance the state,
        // otoh this might be optimized out by the compiler...?
        operator()();
    }
    Xoroshiro128Plus(uint64_t s1, uint64_t s2) : state{s1, s2} { operator()(); }
    void seed(uint64_t s0, uint64_t s1)
    {
        state[0] = s0;
        state[1] = s1;
        operator()();
    }

    bool isSeeded() { return state[0] || state[1]; }

    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    uint64_t operator()()
    {
        uint64_t s0 = state[0];
        uint64_t s1 = state[1];
        uint64_t result = s0 + s1;

        s1 ^= s0;
        state[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14);
        state[1] = rotl(s1, 36);

        return result;
    }
    constexpr uint64_t min() const { return 0; }
    constexpr uint64_t max() const { return UINT64_MAX; }
    double nextFloat64() { return (*this)() * 5.421010862427522e-20; }
    uint64_t nextUint64() { return (*this)(); }
    uint32_t nextUint32()
    {
        // Take top 32 bits which has better randomness properties
        return operator()() >> 32;
    }
    float nextFloat() { return nextUint32() * 2.32830629e-10f; }
    float nextFloatInRange(float minvalue, float maxvalue)
    {
        return mapvalue(nextFloat(), 0.0f, 1.0f, minvalue, maxvalue);
    }
    double nextFloat64InRange(double minvalue, double maxvalue)
    {
        return mapvalue(nextFloat64(), 0.0, 1.0, minvalue, maxvalue);
    }
    int nextInt32InRange(int minval, int maxval)
    {
        assert(maxval > minval);
        return minval + (nextUint32() % (maxval - minval));
    }
    double nextCauchy(double location, double scale)
    {
        double z = nextFloat64();
        return location + scale * std::tan(M_PI * (z - 0.5));
    }
    // pretty good substitute for Gauss
    double nextHypCos(double location, double scale)
    {
        // we can't do the final calculation with exactly 0.0 or 1.0, so clamp
        // there might be some other ways to deal with this, but this shall suffice for now
        double z = std::clamp(nextFloat64(), std::numeric_limits<double>::epsilon(),
                              1.0 - std::numeric_limits<double>::epsilon());
        return location + scale * (2.0 / M_PI * std::log(std::tan(M_PI / 2.0 * z)));
    }
};

inline double custom_log(double value, double base) { return std::log(value) / std::log(base); }

inline void sanitizeRange(float &a, float &b, float mindiff)
{
    if (b < a)
        std::swap(a, b);
    if (b - a < mindiff)
        b += mindiff;
}

enum Distributions
{
    DIST_UNIFORM,
    DIST_HYPCOS,
    DIST_CAUCHY,
    DIST_LAST
};

enum ResetModes
{
    RM_Zeros,
    RM_Avg,
    RM_Min,
    RM_Max,
    RM_UniformRandom,
    RM_BinaryRandom,
    LASTRM
};

inline float avg(float a, float b) { return a + (b - a) / 2.0f; }

class GendynNode
{
  public:
    GendynNode() {}
    float m_x_prim = 0.0f;
    float m_y_prim = 0.0f;
    float m_x_sec = 0.0f;
    float m_y_sec = 0.0f;
};

class DCBlocker
{
  public:
    DCBlocker() {}
    float process(float input)
    {
        double y = input - xm1 + 0.995 * ym1;
        xm1 = input;
        ym1 = y;
        return y;
    }

  private:
    double xm1 = 0.0f;
    double ym1 = 0.0f;
};

class GendynOsc
{
  public:
    GendynOsc()
    {
        m_nodes.resize(128);
        for (int i = 0; i < 128; ++i)
        {
            m_nodes[i].m_x_prim = avg(m_time_primary_high_barrier, m_time_primary_high_barrier);
            m_nodes[i].m_x_sec = avg(m_time_secondary_low_barrier, m_time_secondary_high_barrier);
        }
        m_cur_dur = m_nodes.front().m_x_sec;
        m_cur_y0 = m_nodes.front().m_y_sec;
        m_cur_y1 = m_nodes[1].m_y_sec;
        m_next_segment_time = m_nodes[0].m_x_sec;
        setSampleRate(44100.0f);
    }
    void setRandomSeed(int s) { m_rand.seed(s, 7); }
    void process(float *buf, int nframes)
    {
        for (int i = 0; i < nframes; ++i)
        {
            float t1 = m_cur_dur;

            float y0 = m_cur_y0;
            float y1 = m_cur_y1;
            float s = y0 + (y1 - y0) / t1 * m_phase;
            s = m_hpfilt.process(s);
            buf[i] = s; // clamp(s,-1.0f,1.0f);
            m_phase += 1.0;
            // m_segment_phase += 1.0;
            if (m_phase >= m_next_segment_time)
            {
                ++m_cur_node;

                if (m_cur_node < m_num_segs - 1)
                {
                    m_cur_dur = m_nodes[m_cur_node].m_x_sec;
                    m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
                    m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
                    m_next_segment_time = m_cur_dur;
                }
                if (m_cur_node == m_num_segs - 1)
                {
                    m_cur_dur = m_nodes[m_cur_node].m_x_sec;
                    m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
                    m_next_segment_time = m_cur_dur;
                    if (m_deferred_num_segs > 0)
                    {
                        m_num_segs = m_deferred_num_segs;
                        m_deferred_num_segs = 0;
                    }

                    updateTable();
                    m_cur_y1 = m_nodes.front().m_y_sec;
                    m_cur_node = 0;
                }
                /*
                                if (m_cur_node == m_num_segs)
                                {
                                        m_cur_node = 0;
                                        m_cur_dur = m_nodes[m_cur_node].m_x_sec;
                                        m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
                                        m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
                                        m_next_segment_time = m_cur_dur;

                                }
                */
                m_phase = 0.0;
            }
        }
    }
    void resetTable()
    {
        std::uniform_real_distribution<float> ampdist{m_amp_secondary_low_barrier,
                                                      m_amp_secondary_high_barrier};
        std::uniform_real_distribution<float> timedist{m_time_secondary_low_barrier,
                                                       m_time_secondary_high_barrier};
        std::uniform_real_distribution<float> unidist(0.0, 1.0);
        for (int i = 0; i < m_num_segs; ++i)
        {
            m_nodes[i].m_x_prim = avg(m_time_primary_low_barrier, m_time_primary_high_barrier);
            if (m_timeResetMode == RM_Avg)
                m_nodes[i].m_x_sec =
                    avg(m_time_secondary_low_barrier, m_time_secondary_high_barrier);
            else if (m_timeResetMode == RM_BinaryRandom)
            {
                if (unidist(m_rand) < 0.5)
                    m_nodes[i].m_x_sec = m_time_secondary_low_barrier;
                else
                    m_nodes[i].m_x_sec = m_time_secondary_high_barrier;
            }
            else
            {
                m_nodes[i].m_x_sec = m_sampleRate / m_center_frequency / m_num_segs;
            }
            m_nodes[i].m_y_prim = 0.0f;
            if (m_ampResetMode == RM_Zeros)
                m_nodes[i].m_y_sec = 0.0f;
            else if (m_ampResetMode == RM_UniformRandom)
                m_nodes[i].m_y_sec = ampdist(m_rand);
            else
                m_nodes[i].m_y_sec = 0.0f;
        }
        m_cur_node = 0;
        m_phase = 0.0;
        m_next_segment_time = m_nodes[0].m_x_sec;
        // m_segment_phase = 0.0;
        m_cur_dur = m_nodes[m_cur_node].m_x_sec;
        m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
        m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
    }
    void setFrequencies(float center, float a, float b)
    {
        m_center_frequency = center;
        float hz = center * pow(2.0, (1.0 / 12.0 * a));
        m_low_frequency = hz;
        m_time_secondary_high_barrier = clamp(m_sampleRate / hz / m_num_segs, 1.0f, 128.0f);
        hz = center * pow(2.0, (1.0 / 12.0 * b));
        m_high_frequency = hz;
        m_time_secondary_low_barrier = clamp(m_sampleRate / hz / m_num_segs, 1.0, 128.0f);
        sanitizeRange(m_time_secondary_low_barrier, m_time_secondary_high_barrier, 1.0f);
    }
    void updateTable()
    {
        m_amp_primary_low_barrier = -rescale(m_amp_flux, 0.0f, 1.0f, 0.01, 1.0f);
        m_amp_primary_high_barrier = -m_amp_primary_low_barrier;
        m_amp_dev = m_amp_flux * (m_amp_primary_high_barrier - m_amp_primary_low_barrier);
        // std::normal_distribution<float> timedist(m_time_mean, m_time_dev);
        std::normal_distribution<float> ampdist(m_amp_mean, m_amp_dev);
        float segAcc = 0.0f;
        for (int i = 0; i < m_num_segs; ++i)
        {
            float x_p = m_nodes[i].m_x_prim;
            if (m_time_dist == Distributions::DIST_HYPCOS)
                x_p += m_rand.nextHypCos(m_time_mean, m_time_dev);
            else if (m_time_dist == Distributions::DIST_CAUCHY)
                x_p += m_rand.nextCauchy(m_time_mean, m_time_dev);
            else
                x_p += m_rand.nextHypCos(m_time_mean, m_time_dev);
            x_p = reflect_value(m_time_primary_low_barrier, x_p, m_time_primary_high_barrier);
            float x_s = m_nodes[i].m_x_sec;
            x_s += x_p;
            float secbar0 = m_time_secondary_low_barrier;
            float secbar1 = m_time_secondary_high_barrier;
            sanitizeRange(secbar0, secbar1, 1.0f);
            x_s = reflect_value(secbar0, x_s, secbar1);
            m_nodes[i].m_x_prim = x_p;
            m_nodes[i].m_x_sec = x_s;
            segAcc += m_nodes[i].m_x_sec;
            float y_p = m_nodes[i].m_y_prim;
            y_p += ampdist(m_rand);
            y_p = clamp(y_p, m_amp_primary_low_barrier, m_amp_primary_high_barrier);
            float y_s = m_nodes[i].m_y_sec;
            y_s += y_p;
            y_s = reflect_value(m_amp_secondary_low_barrier, y_s, m_amp_secondary_high_barrier);
            m_nodes[i].m_y_prim = y_p;
            m_nodes[i].m_y_sec = y_s;
        }
        float freq = m_sampleRate / segAcc;
        float volts = std::log2(freq / rack::dsp::FREQ_C4);
        m_curFrequencyVolts = clamp(volts, -5.0, 5.0);
        // m_next_segment_time = m_nodes[0].m_x_sec;
    }
    int m_num_segs = 11;
    int m_deferred_num_segs = 0;
    float m_time_primary_low_barrier = -1.0;
    float m_time_primary_high_barrier = 1.0;
    float m_time_secondary_low_barrier = 5.0;
    float m_time_secondary_high_barrier = 20.0;

    Distributions m_time_dist = Distributions::DIST_HYPCOS;
    float m_time_mean = 0.0f;
    float m_time_dev = 0.01;

    int m_timeResetMode = RM_Avg;
    int m_ampResetMode = RM_Zeros;
    float m_center_frequency = 440.0f;
    float m_low_frequency = 440.0f;
    float m_high_frequency = 440.0f;
    float m_curFrequencyVolts = 0.0f;
    void setNumSegments(int n)
    {
        m_deferred_num_segs = clamp(n, 3, 64);
        return;
        if (n != m_num_segs)
        {
            m_num_segs = clamp(n, 3, 64);
            // if (m_num_segs>=m_cur_node)
            {
                m_cur_node = 0;
                m_phase = 0.0;
                m_cur_dur = m_nodes[m_cur_node].m_x_sec;
                m_cur_y0 = m_nodes[m_cur_node].m_y_sec;
                m_cur_y1 = m_nodes[m_cur_node + 1].m_y_sec;
            }
        }
    }
    void setSampleRate(float s)
    {
        if (s != m_sampleRate)
        {
            m_sampleRate = s;
            float normfreq = 50.0 / m_sampleRate;
            float q = sqrt(2.0) / 2.0;
            m_hpfilt.setParameters(dsp::BiquadFilter::HIGHPASS, normfreq, q, 1.0f);
        }
    }
    void setAmplitudeFlux(float f) { m_amp_flux = clamp(f, 0.0f, 1.0f); }

  private:
    int m_cur_node = 0;
    double m_phase = 0.0;
    // double m_segment_phase = 0.0;
    double m_next_segment_time = 0.0;
    std::vector<GendynNode> m_nodes;
    Xoroshiro128Plus m_rand;
    float m_cur_dur = 0.0;
    float m_cur_y0 = 0.0;
    float m_cur_y1 = 0.0;
    float m_sampleRate = 0.0f;
    float m_amp_primary_low_barrier = -0.05;
    float m_amp_primary_high_barrier = 0.05;
    float m_amp_secondary_low_barrier = -0.9;
    float m_amp_secondary_high_barrier = 0.9;
    float m_amp_mean = 0.0f;
    float m_amp_dev = 0.01;
    float m_amp_flux = -1.0f;
    dsp::TBiquadFilter<float> m_hpfilt;
    DCBlocker m_dcblock;
};

class GendynModule : public rack::Module
{
  public:
    enum PARAMS
    {
        PAR_NUM_SEGS,
        PAR_TIME_DISTRIBUTION,
        PAR_TIME_RESET_MODE,
        PAR_TIME_PRIMARY_BARRIER_LOW,
        PAR_TIME_PRIMARY_BARRIER_HIGH,
        PAR_TimeSecondaryBarrierLow,
        PAR_TimeSecondaryBarrierHigh,
        PAR_TIME_MEAN,
        PAR_TIME_DEVIATION,
        PAR_AMP_RESET_MODE,
        PAR_AMP_BEHAVIOR,
        PAR_PolyphonyVoices,
        PAR_CENTER_FREQUENCY,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_RESET,
        IN_PITCH,
        IN_PITCH_FLUX,
        IN_AMP_FLUX,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_AUDIO,
        OUT_PITCH,
        OUT_LAST
    };
    int m_numvoices_used = 0;
    GendynModule();
    std::string getDebugMessage();
    void process(const ProcessArgs &args) override;

  private:
    GendynOsc m_oscs[16];
    dsp::SchmittTrigger m_reset_trigger;
    dsp::ClockDivider m_divider;
};

class GendynWidget : public ModuleWidget
{
  public:
    GendynWidget(GendynModule *m);
    void draw(const DrawArgs &args) override;
};

GendynModule::GendynModule()
{
    for (int i = 0; i < 16; ++i)
        m_oscs[i].setRandomSeed(i);
    config(PARAMS::PAR_LAST, IN_LAST, OUT_LAST);
    configParam(PAR_NUM_SEGS, 3.0, 64.0, 10.0, "Num segments");
    configSwitch(PAR_TIME_DISTRIBUTION, 0.0, DIST_LAST - 1, 1.0, "Time distribution",
                 {{"Uniform"}, {"HypCos (Gauss-like)"}, {"Cauchy"}});
    configParam(PAR_TIME_MEAN, -5.0, 5.0, 0.0, "Time mean");
    configParam(PAR_TIME_RESET_MODE, 0.0, LASTRM, RM_Avg, "Time reset mode");
    configParam(PAR_TIME_DEVIATION, 0.0, 5.0, 0.1, "Time deviation");
    configParam(PAR_TIME_PRIMARY_BARRIER_LOW, -5.0, 5.0, -1.0, "Time primary low barrier");
    configParam(PAR_TIME_PRIMARY_BARRIER_HIGH, -5.0, 5.0, 1.0, "Time primary high barrier");
    configParam(PAR_TimeSecondaryBarrierLow, -60.0, 60.0, -1.0, "Time sec low barrier");
    configParam(PAR_TimeSecondaryBarrierHigh, -60.0, 60.0, 1.0, "Time sec high barrier");
    configParam(PAR_AMP_RESET_MODE, 0.0, LASTRM, RM_UniformRandom, "Amp reset mode");
    configParam(PAR_PolyphonyVoices, 0.0, 16.0, 0, "Polyphony voices");
    configParam(PAR_CENTER_FREQUENCY, -54.f, 54.f, 0.f, "Center frequency", " Hz",
                dsp::FREQ_SEMITONE, dsp::FREQ_C4);
    configParam(PAR_AMP_BEHAVIOR, 0.0, 1.0f, 0.1f, "Amplitude flux");
    m_divider.setDivision(16);
}

std::string GendynModule::getDebugMessage()
{
    std::stringstream ss;
    ss << m_oscs[0].m_low_frequency << " " << m_oscs[0].m_center_frequency << " ";
    ss << m_oscs[0].m_high_frequency << " " << m_oscs[0].m_time_secondary_low_barrier << " ";
    ss << m_oscs[0].m_time_secondary_high_barrier << " " << m_numvoices_used;
    return ss.str();
}

void GendynModule::process(const ProcessArgs &args)
{
    int numvoices = params[PAR_PolyphonyVoices].getValue();
    if (numvoices == 0 && inputs[IN_PITCH].isConnected())
        numvoices = inputs[IN_PITCH].getChannels();
    if (numvoices == 0)
        numvoices = 1;
    bool shouldReset = false;
    if (m_reset_trigger.process(inputs[IN_RESET].getVoltage()))
    {
        shouldReset = true;
    }
    m_numvoices_used = numvoices;
    outputs[0].setChannels(numvoices);
    outputs[1].setChannels(numvoices);
    float numsegs = params[PAR_NUM_SEGS].getValue();
    numsegs = clamp(numsegs, 3.0, 64.0);
    const float timedev_base = params[PAR_TIME_DEVIATION].getValue();
    float sectimebarlow = params[PAR_TimeSecondaryBarrierLow].getValue();
    sectimebarlow = clamp(sectimebarlow, 1.0, 64.0);
    float sectimebarhigh = params[PAR_TimeSecondaryBarrierHigh].getValue();
    sectimebarhigh = clamp(sectimebarhigh, 1.0, 64.0);
    sanitizeRange(sectimebarlow, sectimebarhigh, 1.0f);
    const float aflux_base = params[PAR_AMP_BEHAVIOR].getValue();
    const float pitch_base = params[PAR_CENTER_FREQUENCY].getValue();
    if (m_divider.process())
    {
        for (int i = 0; i < numvoices; ++i)
        {
            m_oscs[i].setSampleRate(args.sampleRate);
            m_oscs[i].m_time_dist = (Distributions)(int)params[PAR_TIME_DISTRIBUTION].getValue();
            m_oscs[i].setNumSegments(numsegs);
            float timedev = timedev_base + 2.5 * inputs[IN_PITCH_FLUX].getVoltage(i);
            timedev = clamp(timedev, 0.0f, 5.0f);
            m_oscs[i].m_time_dev = timedev;
            m_oscs[i].m_time_mean = params[PAR_TIME_MEAN].getValue();

            float pitch =
                pitch_base + rescale(inputs[IN_PITCH].getVoltage(i), -5.0f, 5.0f, -60.0f, 60.0f);
            pitch = clamp(pitch, -60.0f, 60.0f);
            float centerfreq = dsp::FREQ_C4 * pow(2.0f, 1.0f / 12.0f * pitch);
            m_oscs[i].setFrequencies(centerfreq, params[PAR_TimeSecondaryBarrierLow].getValue(),
                                     params[PAR_TimeSecondaryBarrierHigh].getValue());
            // m_oscs[i].m_time_secondary_low_barrier = sectimebarlow;
            // m_oscs[i].m_time_secondary_high_barrier = sectimebarhigh;
            float bar0 = params[PAR_TIME_PRIMARY_BARRIER_LOW].getValue();
            float bar1 = params[PAR_TIME_PRIMARY_BARRIER_HIGH].getValue();
            if (bar1 <= bar0)
                bar1 = bar0 + 0.01;
            m_oscs[i].m_time_primary_low_barrier = bar0;
            m_oscs[i].m_time_primary_high_barrier = bar1;
            float aflux = aflux_base + 0.1 * inputs[IN_AMP_FLUX].getVoltage(i);
            // osc clamps
            m_oscs[i].setAmplitudeFlux(aflux);
        }
    }
    if (shouldReset == true)
    {
        for (int i = 0; i < numvoices; ++i)
        {
            m_oscs[i].m_ampResetMode = params[PAR_AMP_RESET_MODE].getValue();
            m_oscs[i].m_timeResetMode = params[PAR_TIME_RESET_MODE].getValue();
            float pitch = params[PAR_CENTER_FREQUENCY].getValue();
            pitch += rescale(inputs[IN_PITCH].getVoltage(i), -5.0f, 5.0f, -60.0f, 60.0f);
            pitch = clamp(pitch, -60.0f, 60.0f);
            float centerfreq = dsp::FREQ_C4 * pow(2.0f, 1.0f / 12.0f * pitch);
            m_oscs[i].setFrequencies(centerfreq, params[PAR_TimeSecondaryBarrierLow].getValue(),
                                     params[PAR_TimeSecondaryBarrierHigh].getValue());
            m_oscs[i].resetTable();
        }
    }
    for (int i = 0; i < numvoices; ++i)
    {
        float outsample = 0.0f;
        m_oscs[i].process(&outsample, 1);

        outputs[1].setVoltage(m_oscs[i].m_curFrequencyVolts, i);
        outputs[0].setVoltage(outsample * 5.0f, i);
    }
}

GendynWidget::GendynWidget(GendynModule *m)
{
    setModule(m);
    box.size.x = RACK_GRID_WIDTH * 23;
    auto port = new PortWithBackGround(m, this, GendynModule::OUT_AUDIO, 1, 30, "AUDIO OUT", true);
    port = new PortWithBackGround(m, this, GendynModule::OUT_PITCH, 31, 30, "PITCH OUT", true);
    port = new PortWithBackGround(m, this, GendynModule::IN_RESET, 62, 30, "RESET", false);
    float xc = 1.0f;
    float yc = 80.0f;
    addChild(new KnobInAttnWidget(this, "PITCH", GendynModule::PAR_CENTER_FREQUENCY,
                                  GendynModule::IN_PITCH, -1, xc, yc));
    xc += 82.0f;
    addChild(new KnobInAttnWidget(this, "PITCH FLUX", GendynModule::PAR_TIME_DEVIATION,
                                  GendynModule::IN_PITCH_FLUX, -1, xc, yc));
    xc += 82.0f;
    addChild(new KnobInAttnWidget(this, "PITCH MIN", GendynModule::PAR_TimeSecondaryBarrierLow, -1,
                                  -1, xc, yc));
    xc += 82.0f;
    addChild(new KnobInAttnWidget(this, "PITCH MAX", GendynModule::PAR_TimeSecondaryBarrierHigh, -1,
                                  -1, xc, yc));
    yc += 47;
    xc = 1;
    addChild(new KnobInAttnWidget(this, "AMPLITUDE FLUX", GendynModule::PAR_AMP_BEHAVIOR,
                                  GendynModule::IN_AMP_FLUX, -1, xc, yc));
    xc += 82.0f;
    addChild(new KnobInAttnWidget(this, "NUM SEGMENTS", GendynModule::PAR_NUM_SEGS, -1, -1, xc, yc,
                                  true));
    xc += 82.0f;
    addChild(new KnobInAttnWidget(this, "TIME DISTRIBUTION", GendynModule::PAR_TIME_DISTRIBUTION,
                                  -1, -1, xc, yc, true));
}

void GendynWidget::draw(const DrawArgs &args)
{
    nvgSave(args.vg);
    float w = box.size.x;
    float h = box.size.y;
    nvgBeginPath(args.vg);
    nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
    nvgRect(args.vg, 0.0f, 0.0f, w, h);
    nvgFill(args.vg);

    nvgFontSize(args.vg, 15);
    nvgFontFaceId(args.vg, getDefaultFont(1)->handle);
    nvgTextLetterSpacing(args.vg, -1);
    nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(args.vg, 3, 10, "GenDyn", NULL);
    nvgText(args.vg, 3, h - 11, "Xenakios", NULL);
    if (module)
    {
        GendynModule *mod = dynamic_cast<GendynModule *>(module);
        nvgText(args.vg, 1, 20, mod->getDebugMessage().c_str(), NULL);
    }
    nvgRestore(args.vg);
    ModuleWidget::draw(args);
}

Model *modelGendynOSC = createModel<GendynModule, GendynWidget>("GendynOsc");
