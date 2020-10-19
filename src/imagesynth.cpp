#include "plugin.hpp"
#include <random>
#include <stb_image.h>
#include <atomic>
#include <functional>
#include <thread> 
#include "wdl/resample.h"

extern std::shared_ptr<Font> g_font;

template <typename T>
inline T triplemax (T a, T b, T c)                           
{ 
    return a < b ? (b < c ? c : b) : (a < c ? c : a); 
}

inline float harmonics3(float xin)
{
    return 0.5 * std::sin(xin) + 0.25 * std::sin(xin * 2.0) + 0.1 * std::sin(xin * 3);
}

inline float harmonics4(float xin)
{
    return 0.5 * std::sin(xin) + 0.25 * std::sin(xin * 2.0) + 0.1 * std::sin(xin * 3) +
        0.15*std::sin(xin*7);
}

class ImgWaveOscillator
{
public:
    void initialise(std::function<float(float)> f, 
    int tablesize)
    {
        m_tablesize = tablesize;
        m_table.resize(tablesize);
        for (int i=0;i<tablesize;++i)
            m_table[i] = f(rescale(i,0,tablesize-1,-3.141592653,3.141592653));
    }
    void setFrequency(float hz)
    {
        m_phaseincrement = m_tablesize*hz*(1.0/m_sr);
        m_freq = hz;
    }
    float processSample(float)
    {
        int index = m_phase;
        float sample = m_table[index];
        m_phase+=m_phaseincrement;
        if (m_phase>=m_tablesize)
            m_phase-=m_tablesize;
        return sample;
    }
    void prepare(int numchans, float sr)
    {
        m_sr = sr;
        setFrequency(m_freq);
    }
    void reset(float initphase)
    {
        m_phase = initphase;
    }
private:
    int m_tablesize = 0;
    std::vector<float> m_table;
    double m_phase = 0.0;
    float m_sr = 44100.0f;
    float m_phaseincrement = 0.0f;
    float m_freq = 440.0f;
};

class ImgOscillator
{
public:
    float* m_gainCurve = nullptr;
    ImgOscillator()
    {
        for (int i = 0; i < 4; ++i)
            m_pan_coeffs[i] = 0.0f;
        m_osc.initialise([](float x)
            {
                return 0.5 * std::sin(x) + 0.25 * std::sin(x * 2.0) + 0.1 * std::sin(x * 3);

            }, 1024);
    }
    void setFrequency(float hz)
    {
        m_osc.setFrequency(hz);
    }
    void setEnvelopeAmount(float amt)
    {
        a = rescale(amt, 0.0f, 1.0f, 0.9f, 0.9999f);
        b = 1.0 - a;
    }
    void generate(float pix_mid_gain)
    {
        int gain_index = rescale(pix_mid_gain, 0.0f, 1.0f, 0, 255);
        pix_mid_gain = m_gainCurve[gain_index];
        float z = (pix_mid_gain * b) + (m_env_state * a);
        if (z < m_cut_th)
            z = 0.0;
        m_env_state = z;
        if (z > 0.00)
        {
            outSample = z * m_osc.processSample(0.0f);
        }
        else
            outSample = 0.0f;

    }
    float outSample = 0.0f;
    //private:
    ImgWaveOscillator m_osc;
    float m_env_state = 0.0f;
    float m_pan_coeffs[4];
    float m_cut_th = 0.0f;
    float a = 0.998;
    float b = 1.0 - a;
};

class ImgSynth
{
public:
    std::mt19937 m_rng{ 99937 };
    ImgSynth()
    {
        m_pixel_to_gain_table.resize(256);
        m_oscillators.resize(1024);
        m_freq_gain_table.resize(1024);
    }
    stbi_uc* m_img_data = nullptr;
    int m_img_w = 0;
    int m_img_h = 0;
    void setImage(stbi_uc* data, int w, int h)
    {
        m_img_data = data;
        m_img_w = w;
        m_img_h = h;
        float thefundamental = rack::dsp::FREQ_C4 * pow(2.0, 1.0 / 12 * m_fundamental);
        for (int i = 0; i < (int)m_oscillators.size(); ++i)
        {
            if (m_frequencyMapping == 0)
            {
                float pitch = rescale(i, 0, h, 102.0, 0.0);
                float frequency = 32.0 * pow(2.0, 1.0 / 12 * pitch);
                m_oscillators[i].m_osc.setFrequency(frequency);
            }
            if (m_frequencyMapping == 1)
            {
                float frequency = rescale(i, 0, h, 7000.0f, 32.0f);
                m_oscillators[i].m_osc.setFrequency(frequency);
            }
            if (m_frequencyMapping == 2)
            {
                int harmonic = rescale(i, 0, h, 128.0f, 1.0f);
                m_oscillators[i].m_osc.setFrequency(thefundamental * harmonic);
            }
            float curve_begin = 1.0f - m_freq_response_curve;
            float curve_end = m_freq_response_curve;
            float resp_gain = rescale(i, 0, h, curve_end, curve_begin);
            m_freq_gain_table[i] = resp_gain;
            
        }

    }

    void render(float outdur, float sr)
    {
        m_shouldCancel = false;
        m_elapsedTime = 0.0;
        std::uniform_real_distribution<float> dist(0.0, 3.141);
        //double t0 = juce::Time::getMillisecondCounterHiRes();
        const float cut_th = rack::dsp::dbToAmplitude(-72.0f);
        m_maxGain = 0.0f;
        m_percent_ready = 0.0f;
        m_renderBuf.resize(m_numOutChans* ((1.0 + outdur) * sr));
        
        for (int i = 0; i < 256; ++i)
        {
            m_pixel_to_gain_table[i] = std::pow(1.0 / 256 * i,m_pixel_to_gain_curve);
        }
        
        std::uniform_real_distribution<float> pandist(0.0, 3.141592653 / 2.0f);
        for (int i = 0; i < (int)m_oscillators.size(); ++i)
        {
            m_oscillators[i].m_osc.prepare(1,sr);
            m_oscillators[i].m_osc.reset(dist(m_rng));
            m_oscillators[i].m_env_state = 0.0f;
            m_oscillators[i].m_cut_th = cut_th;
            m_oscillators[i].setEnvelopeAmount(m_envAmount);
            m_oscillators[i].m_gainCurve = m_pixel_to_gain_table.data();
            if (m_waveFormType == 0)
                m_oscillators[i].m_osc.initialise([](float xin){ return std::sin(xin); },1024);
            else if (m_waveFormType == 1)
                m_oscillators[i].m_osc.initialise([](float xin)
                                                { return harmonics3(xin);},1024);
            else if (m_waveFormType == 2)
                m_oscillators[i].m_osc.initialise([](float xin)
                                                  { return harmonics4(xin);},1024);
            if (m_panMode == 0)
            {
                if (m_numOutChans == 2)
                {
                    float panpos = pandist(m_rng);
                    m_oscillators[i].m_pan_coeffs[0] = std::cos(panpos);
                    m_oscillators[i].m_pan_coeffs[1] = std::sin(panpos);
                }
                else if (m_numOutChans == 4)
                {
                    float angle = pandist(m_rng) * 2.0f; // position along circle
                    float panposx = rescale(std::cos(angle), -1.0f, 1.0, 0.0f, 3.141592653);
                    float panposy = rescale(std::sin(angle), -1.0f, 1.0, 0.0f, 3.141592653);
                    m_oscillators[i].m_pan_coeffs[0] = std::cos(panposx);
                    m_oscillators[i].m_pan_coeffs[1] = std::sin(panposx);
                    m_oscillators[i].m_pan_coeffs[2] = std::cos(panposy);
                    m_oscillators[i].m_pan_coeffs[3] = std::sin(panposy);
                }
            }
            if (m_panMode == 1)
            {
                int outspeaker = i % m_numOutChans;
                for (int j = 0; j < m_numOutChans; ++j)
                {
                    if (j == outspeaker)
                        m_oscillators[i].m_pan_coeffs[j] = 1.0f;
                    else m_oscillators[i].m_pan_coeffs[j] = 0.0f;
                }

            }
            if (m_panMode == 2)
            {
                m_oscillators[i].m_pan_coeffs[0] = 0.71f;
                m_oscillators[i].m_pan_coeffs[1] = 0.71f;
                m_oscillators[i].m_pan_coeffs[2] = 0.71f;
                m_oscillators[i].m_pan_coeffs[3] = 0.71f;
            }
        }
        m_renderBuf.clear();
        //auto outbuf = m_renderBuf.getArrayOfWritePointers();
        int imgw = m_img_w;
        int imgh = m_img_h;
        int outdursamples = sr * outdur;
        
        for (int x = 0; x < outdursamples; x += m_stepsize)
        {
            if (m_shouldCancel)
                break;
            m_percent_ready = 1.0 / outdursamples * x;

            for (int y = 0; y < imgh; ++y)
            {
                int xcor = rescale(x, 0, outdursamples, 0, imgw);
                if (xcor>=imgw)
                    xcor = imgw-1;
                if (xcor<0)
                    xcor = 0;
                const stbi_uc *p = m_img_data + (4 * (y * imgw + xcor));
                unsigned char r = p[0];
                unsigned char g = p[1];
                unsigned char b = p[2];
                //unsigned char a = p[3];
                float pix_mid_gain = (float)triplemax(r,g,b)/255.0f;
                //float pix_mid_gain = (r / 255.0) * 0.3 + (g / 255.0) * 0.59 + (b / 255.0) * 0.11;
                
                //float pix_mid_gain = 0.0f;
                for (int i = 0; i < m_stepsize; ++i)
                {
                    m_oscillators[y].generate(pix_mid_gain);
                    float sample = m_oscillators[y].outSample;
                    if (fabs(sample) > 0.0f)
                    {
                        float resp_gain = m_freq_gain_table[y];
                        for (int chan = 0; chan < m_numOutChans; ++chan)
                        {
                            m_renderBuf[(x + i)*m_numOutChans+chan] += sample * 0.1f * resp_gain * m_oscillators[y].m_pan_coeffs[chan];
                        }
                    }
                }

            }

        }
        if (!m_shouldCancel)
        {
            //m_renderBuf.applyGainRamp(outdursamples - 512, 512 + m_stepsize, 1.0f, 0.0f);
            auto it = std::max_element(m_renderBuf.begin(),m_renderBuf.end());
            m_maxGain = *it; 
            //m_elapsedTime = juce::Time::getMillisecondCounterHiRes() - t0;
        }
        m_percent_ready = 1.0;
    }

    float percentReady()
    {
        return m_percent_ready;
    }
    std::vector<float> m_renderBuf;
    float m_maxGain = 0.0f;
    double m_elapsedTime = 0.0f;
    std::atomic<bool> m_shouldCancel{ false };
    int m_frequencyMapping = 0;
    float m_fundamental = -24.0f; // semitones below middle C!
    int m_panMode = 0;
    int m_numOutChans = 2;
    float m_envAmount = 0.95f;
    float m_pixel_to_gain_curve = 1.0f;
    int m_stepsize = 64;
    float m_freq_response_curve = 0.5f;
    int m_waveFormType = 0;
private:
    
    std::vector<ImgOscillator> m_oscillators;
    std::vector<float> m_freq_gain_table;
    std::vector<float> m_pixel_to_gain_table;
    std::atomic<float> m_percent_ready{ 0.0 };
};



class XImageSynth : public rack::Module
{
public:
    enum Inputs
    {
        IN_PITCH_CV,
        IN_RESET,
        IN_LOOPSTART_CV,
        IN_LOOPLEN_CV,
        LAST_INPUT
    };
    enum Outputs
    {
        OUT_AUDIO,
        OUT_LOOP_SWITCH,
        LAST_OUTPUT
    };
    enum Parameters
    {
        PAR_RELOAD_IMAGE,
        PAR_DURATION,
        PAR_PITCH,
        PAR_FREQMAPPING,
        PAR_WAVEFORMTYPE,
        PAR_PRESET_IMAGE,
        PAR_LOOP_START,
        PAR_LOOP_LEN,
        PAR_FREQUENCY_BALANCE,
        PAR_HARMONICS_FUNDAMENTAL,
        PAR_PAN_MODE,
        PAR_NUMOUTCHANS,
        PAR_LAST
    };
    int m_comp = 0;
    std::list<std::string> presetImages;
    std::vector<stbi_uc> m_backupdata; 
    dsp::BooleanTrigger reloadTrigger;
    std::atomic<bool> m_renderingImage;
    float loopstart = 0.0f;
    float looplen = 1.0f;
    XImageSynth()
    {
        m_renderingImage = false;
        presetImages = rack::system::getEntries(asset::plugin(pluginInstance, "res/image_synth_images"));
        config(PAR_LAST,LAST_INPUT,LAST_OUTPUT,0);
        configParam(PAR_RELOAD_IMAGE,0,1,1,"Reload image");
        configParam(PAR_DURATION,0.5,60,5.0,"Image duration");
        configParam(PAR_PITCH,-24,24,0.0,"Playback pitch");
        configParam(PAR_FREQMAPPING,0,2,0.0,"Frequency mapping type");
        configParam(PAR_WAVEFORMTYPE,0,2,0.0,"Oscillator type");
        configParam(PAR_PRESET_IMAGE,0,presetImages.size()-1,0.0,"Preset image");
        configParam(PAR_LOOP_START,0.0,0.95,0.0,"Loop start");
        configParam(PAR_LOOP_LEN,0.01,1.00,1.0,"Loop length");
        configParam(PAR_FREQUENCY_BALANCE,0.00,1.00,0.25,"Frequency balance");
        configParam(PAR_HARMONICS_FUNDAMENTAL,-72.0,0.00,-24.00,"Harmonics fundamental");
        configParam(PAR_PAN_MODE,0.0,2.0,0.00,"Frequency panning mode");
        configParam(PAR_NUMOUTCHANS,0.0,4.0,0.00,"Output channels configuration");
        m_syn.m_numOutChans = 2;
        reloadImage();
    }
    void reloadImage()
    {
        if (m_renderingImage==true)
            return;
        auto task=[this]
        {
        
        int iw, ih, comp = 0;
        m_img_data = nullptr;
        int imagetoload = params[PAR_PRESET_IMAGE].getValue();
        auto it = presetImages.begin();
        std::advance(it,imagetoload);
        std::string filename = *it;

        auto tempdata = stbi_load(filename.c_str(),&iw,&ih,&comp,4);

        m_playpos = 0.0f;
        m_bufferplaypos = 0;
        
        m_syn.m_panMode = 0;
        int outconf = params[PAR_NUMOUTCHANS].getValue();
        int numoutchans[5]={2,2,4,8,16};
        m_syn.m_numOutChans=numoutchans[outconf];
        m_img_data = tempdata;
        m_img_data_dirty = true;
        m_syn.m_panMode = params[PAR_PAN_MODE].getValue();
        m_syn.m_frequencyMapping = params[PAR_FREQMAPPING].getValue();
        m_syn.m_freq_response_curve = params[PAR_FREQUENCY_BALANCE].getValue();
        m_syn.m_fundamental = params[PAR_HARMONICS_FUNDAMENTAL].getValue();
        m_syn.m_waveFormType = params[PAR_WAVEFORMTYPE].getValue();
        m_syn.setImage(m_img_data ,iw,ih);
        m_out_dur = params[PAR_DURATION].getValue();
        m_syn.render(m_out_dur,44100);
        m_renderingImage = false;
        };
        m_renderingImage = true;
        std::thread th(task);
        th.detach();
    }
    void process(const ProcessArgs& args) override
    {
        int ochans = m_syn.m_numOutChans;
        outputs[OUT_AUDIO].setChannels(ochans);
        if (m_syn.percentReady()*m_out_dur<0.5)
        {
            outputs[OUT_AUDIO].setVoltage(0.0,0);
            outputs[OUT_AUDIO].setVoltage(0.0,1);
            outputs[OUT_AUDIO].setVoltage(0.0,2);
            outputs[OUT_AUDIO].setVoltage(0.0,3);
            return;
        }
        
        float pitch = params[PAR_PITCH].getValue();
        pitch += inputs[IN_PITCH_CV].getVoltage()*12.0f;
        pitch = clamp(pitch,-36.0,36.0);
        m_src.SetRates(44100 ,44100/pow(2.0,1.0/12*pitch));
        int outlensamps = m_out_dur*args.sampleRate;
        loopstart = params[PAR_LOOP_START].getValue();
        loopstart += inputs[IN_LOOPSTART_CV].getVoltage()/5.0f;
        loopstart = clamp(loopstart,0.0f,1.0f);
        int loopstartsamps = outlensamps*loopstart;
        looplen = params[PAR_LOOP_LEN].getValue();
        looplen += inputs[IN_LOOPLEN_CV].getVoltage()/5.0f;
        looplen = clamp(looplen,0.0f,1.0f);
        int looplensamps = outlensamps*looplen;
        if (looplensamps<256) looplensamps = 256;
        int loopendsampls = loopstartsamps+looplensamps;
        if (loopendsampls>=outlensamps)
            loopendsampls = outlensamps-1;
        int xfadelensamples = 128;
        if (m_bufferplaypos<loopstartsamps)
            m_bufferplaypos = loopstartsamps;
        if (rewindTrigger.process(inputs[IN_RESET].getVoltage()))
            m_bufferplaypos = loopstartsamps;
        double* rsbuf = nullptr;
        int wanted = m_src.ResamplePrepare(1,ochans,&rsbuf);
        for (int i=0;i<wanted;++i)
        {
            float gain_a = 1.0f;
            float gain_b = 0.0f;
            if (m_bufferplaypos>=loopendsampls-xfadelensamples)
            {
                gain_a = rescale(m_bufferplaypos,loopendsampls-xfadelensamples,loopendsampls,1.0f,0.0f);
                gain_b = 1.0-gain_a;
            }
            int xfadepos = m_bufferplaypos-looplensamps;
            if (xfadepos<0) xfadepos = 0;
            
            for (int j=0;j<ochans;++j)
            {
                rsbuf[i*ochans+j] = gain_a * m_syn.m_renderBuf[m_bufferplaypos*ochans+j];
                
                rsbuf[i*ochans+j] += gain_b * m_syn.m_renderBuf[xfadepos*ochans+j];
            }
            ++m_bufferplaypos;
            if (m_bufferplaypos>=loopendsampls)
            {
                m_bufferplaypos = loopstartsamps;
                loopStartPulse.trigger();
            }
            if (loopStartPulse.process(args.sampleTime))
                outputs[OUT_LOOP_SWITCH].setVoltage(10.0f);
            else
                outputs[OUT_LOOP_SWITCH].setVoltage(0.0f);
        }
        double samples_out[16];
        memset(&samples_out,0,sizeof(double)*16);
        m_src.ResampleOut(samples_out,wanted,1,ochans);
        for (int i=0;i<ochans;++i)
        {
            outputs[OUT_AUDIO].setVoltage(samples_out[i]*5.0,i);
        }
        m_playpos = m_bufferplaypos / args.sampleRate;
        
    }
    float m_out_dur = 10.0f;

    float m_playpos = 0.0f;
    int m_bufferplaypos = 0;
    stbi_uc* m_img_data = nullptr;
    bool m_img_data_dirty = false;
    ImgSynth m_syn;
    WDL_Resampler m_src;
    rack::dsp::SchmittTrigger rewindTrigger;
    rack::dsp::PulseGenerator loopStartPulse;
};

class MySmallKnob : public RoundSmallBlackKnob
{
public:
    XImageSynth* m_syn = nullptr;
    MySmallKnob() : RoundSmallBlackKnob()
    {
        
    }
    void onDragEnd(const event::DragEnd& e) override
    {
        RoundSmallBlackKnob::onDragEnd(e);
        if (m_syn && paramQuantity->getValue()!=mLastValue)
        {
            m_syn->reloadImage();
            mLastValue = this->paramQuantity->getValue();
        }
    }
    float mLastValue = 0.0f;
};

class XImageSynthWidget : public ModuleWidget
{
public:
    
    XImageSynth* m_synth = nullptr;
    XImageSynthWidget(XImageSynth* m)
    {
        setModule(m);
        m_synth = m;
        box.size.x = 600.0f;
        if (!g_font)
        	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(30, 330), m, XImageSynth::OUT_AUDIO));
        addInput(createInputCentered<PJ301MPort>(Vec(120, 360), m, XImageSynth::IN_PITCH_CV));
        addInput(createInputCentered<PJ301MPort>(Vec(30, 360), m, XImageSynth::IN_RESET));
        addParam(createParamCentered<LEDBezel>(Vec(60.00, 330), m, XImageSynth::PAR_RELOAD_IMAGE));
        
        MySmallKnob* slowknob = nullptr;
        addParam(slowknob = createParamCentered<MySmallKnob>(Vec(90.00, 330), m, XImageSynth::PAR_DURATION));
        slowknob->m_syn = m;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(120.00, 330), m, XImageSynth::PAR_PITCH));
        addParam(slowknob = createParamCentered<MySmallKnob>(Vec(150.00, 330), m, XImageSynth::PAR_FREQMAPPING));
        slowknob->snap = true;
        slowknob->m_syn = m;
        addParam(slowknob = createParamCentered<MySmallKnob>(Vec(150.00, 360), m, XImageSynth::PAR_WAVEFORMTYPE));
        slowknob->snap = true;
        slowknob->m_syn = m;
        addParam(slowknob = createParamCentered<MySmallKnob>(Vec(180.00, 330), m, XImageSynth::PAR_PRESET_IMAGE));
        slowknob->snap = true;
        slowknob->m_syn = m;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(210.00, 330), m, XImageSynth::PAR_LOOP_START));
        addOutput(createOutputCentered<PJ301MPort>(Vec(240, 330), m, XImageSynth::OUT_LOOP_SWITCH));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(210.00, 360), m, XImageSynth::PAR_LOOP_LEN));
        addParam(slowknob = createParamCentered<MySmallKnob>(Vec(270.00, 330), m, XImageSynth::PAR_FREQUENCY_BALANCE));
        slowknob->m_syn = m;
        addParam(slowknob = createParamCentered<MySmallKnob>(Vec(270.00, 360), m, XImageSynth::PAR_HARMONICS_FUNDAMENTAL));
        slowknob->m_syn = m;
        addParam(slowknob = createParamCentered<MySmallKnob>(Vec(300.00, 330), m, XImageSynth::PAR_PAN_MODE));
        slowknob->m_syn = m;
        slowknob->snap = true;
        addParam(slowknob = createParamCentered<MySmallKnob>(Vec(300.00, 360), m, XImageSynth::PAR_NUMOUTCHANS));
        slowknob->m_syn = m;
        slowknob->snap = true;
        addInput(createInputCentered<PJ301MPort>(Vec(330, 330), m, XImageSynth::IN_LOOPSTART_CV));
        addInput(createInputCentered<PJ301MPort>(Vec(330, 360), m, XImageSynth::IN_LOOPLEN_CV));
    }
    ~XImageSynthWidget()
    {
        //nvgDeleteImage(m_ctx,m_image);
    }
    int imageCreateCounter = 0;
    bool imgDirty = false;
    void step() override
    {
        if (m_synth==nullptr)
            return;
        float p = m_synth->params[0].getValue();
        if (m_synth->reloadTrigger.process(p>0.0f))
        {
            m_synth->reloadImage();
            
        }
        ModuleWidget::step();
    }
    void draw(const DrawArgs &args) override
    {
        m_ctx = args.vg;
        if (m_synth==nullptr)
            return;
        nvgSave(args.vg);
        if ((m_image == 0 && m_synth->m_img_data!=nullptr))
        {
            m_image = nvgCreateImageRGBA(args.vg,1200,600,NVG_IMAGE_GENERATE_MIPMAPS,m_synth->m_img_data);
            ++imageCreateCounter;
        }
        if (m_synth->m_img_data_dirty)
        {
            nvgUpdateImage(args.vg,m_image,m_synth->m_img_data);
            m_synth->m_img_data_dirty = false;
        }
        int imgw = 0;
        int imgh = 0;
        nvgImageSize(args.vg,m_image,&imgw,&imgh);
        if (imgw>0 && imgh>0)
        {
            auto pnt = nvgImagePattern(args.vg,0,0,600.0f,300.0f,0.0f,m_image,1.0f);
            nvgBeginPath(args.vg);
            nvgRect(args.vg,0,0,600,300);
            nvgFillPaint(args.vg,pnt);
            
            nvgFill(args.vg);
        }
            
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
            nvgRect(args.vg,0.0f,300.0f,box.size.x,box.size.y-300);
            nvgFill(args.vg);

            nvgBeginPath(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            
            float xcor = rescale(m_synth->m_playpos,0.0,m_synth->m_out_dur,0,600);
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,300);
            nvgStroke(args.vg);

            float loopstart = m_synth->loopstart;
            xcor = rescale(loopstart,0.0,1.0,0,600);
            nvgBeginPath(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0x00, 0xff));
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,300);
            nvgStroke(args.vg);

            float loopend = m_synth->looplen+loopstart;
            if (loopend>1.0f)
                loopend = 1.0f;

            xcor = rescale(loopend,0.0,1.0,0,600);

            nvgBeginPath(args.vg);
            
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,300);
            nvgStroke(args.vg);

            nvgFontSize(args.vg, 15);
            nvgFontFaceId(args.vg, g_font->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            char buf[100];
            sprintf(buf,"%d %d %d %d",imgw,imgh,m_image,imageCreateCounter);
            nvgText(args.vg, 3 , 10, buf, NULL);
        
        float progr = m_synth->m_syn.percentReady();
        if (progr<1.0)
        {
            float progw = rescale(progr,0.0,1.0,0.0,box.size.x);
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0x00, 0x9f, 0x00, 0xa0));
            nvgRect(args.vg,0.0f,280.0f,progw,20);
            nvgFill(args.vg);
        }
        
        
        //nvgDeleteImage(args.vg,m_image);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
private:
    NVGcontext* m_ctx = nullptr;
    int m_image = 0;
};

Model* modelXImageSynth = createModel<XImageSynth, XImageSynthWidget>("XImageSynth");
