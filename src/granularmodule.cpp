#include "plugin.hpp"
#include "grain_engine/grain_engine.h"
#define DR_WAV_IMPLEMENTATION
#include "grain_engine/dr_wav.h"
#include "helperwidgets.h"
#include <osdialog.h>
#include <thread>
#include <mutex>

class DrWavBuffer
{
public:
    DrWavBuffer() {}
    DrWavBuffer(float* src, drwav_uint64 numFrames)
    {
        m_buf = src;
        m_sz = numFrames;
    }
    ~DrWavBuffer()
    {
        if (m_buf)
            drwav_free(m_buf, nullptr);
    }
    DrWavBuffer(const DrWavBuffer&) = delete;
    DrWavBuffer& operator=(const DrWavBuffer&) = delete;
    DrWavBuffer(DrWavBuffer&& other)
    {
        m_buf = other.m_buf;
        m_sz = other.m_sz;
        other.m_buf = nullptr;
        other.m_sz = 0;
    }
    DrWavBuffer& operator=(DrWavBuffer&& other)
    {
        std::swap(m_buf,other.m_buf);
        std::swap(m_sz,other.m_sz);
        return *this;
    }
    float* data() { return m_buf; }
    drwav_uint64 size() { return m_sz; }
private:
    float* m_buf = nullptr;
    drwav_uint64 m_sz = 0;
};

class DrWavSource : public GrainAudioSource
{
public:
    float* m_pSampleData = nullptr;
    unsigned int m_channels = 0;
    unsigned int m_sampleRate = 0;
    drwav_uint64 m_totalPCMFrameCount = 0;
    std::mutex m_mut;
    void normalize(float level)
    {
        if (!m_pSampleData)
            return;
        float peak = 0.0f;
        for (int i=0;i<m_totalPCMFrameCount*m_channels;++i)
        {
            float s = std::fabs(m_pSampleData[i]);
            peak = std::max(s,peak);
        }
        float normfactor = 1.0f;
        if (peak>0.0f)
            normfactor = level/peak;
        for (int i=0;i<m_totalPCMFrameCount*m_channels;++i)
            m_pSampleData[i]*=normfactor;
        updatePeaks();
    }
    void reverse()
    {
        if (!m_pSampleData)
            return;
        for (int i=0;i<m_totalPCMFrameCount/2;i++)
        {
            int index=(m_totalPCMFrameCount-i-1);
            if (index<0 || index>=m_totalPCMFrameCount) break;
            for (int j=0;j<m_channels;j++)
            {
                std::swap(m_pSampleData[i*m_channels+j],m_pSampleData[index*m_channels+j]);
            }
        }
        updatePeaks();
    }
    void updatePeaks()
    {
        peaksData.resize(m_channels);
        int samplesPerPeak = 128;
        int numPeaks = m_totalPCMFrameCount/samplesPerPeak;
        for (int i=0;i<m_channels;++i)
        {
            peaksData[i].resize(numPeaks);
        }
        for (int i=0;i<m_channels;++i)
        {
            int sampleCounter = 0;
            for (int j=0;j<numPeaks;++j)
            {
                float minsample = std::numeric_limits<float>::max();
                float maxsample = std::numeric_limits<float>::min();
                for (int k=0;k<samplesPerPeak;++k)
                {
                    int index = sampleCounter*m_channels+i;
                    float sample = 0.0f;
                    if (index<m_totalPCMFrameCount*m_channels)
                        sample = m_pSampleData[index];
                    minsample = std::min(minsample,sample);
                    maxsample = std::max(maxsample,sample);
                    ++sampleCounter;
                }
                peaksData[i][j].minpeak = minsample;
                peaksData[i][j].maxpeak = maxsample;
            }
        } 
    }
    bool importFile(std::string filename)
    {
        float* pSampleData = nullptr;
        unsigned int channels = 0;
        unsigned int sampleRate = 0;
        drwav_uint64 totalPCMFrameCount = 0;
        pSampleData = drwav_open_file_and_read_pcm_frames_f32(
            filename.c_str(), 
            &channels, 
            &sampleRate, 
            &totalPCMFrameCount, 
            NULL);

        if (pSampleData == NULL) {
            std::cout << "could not open wav with dr wav\n";
            return false;
        }
        float* oldData = m_pSampleData;
        m_mut.lock();
        
            m_channels = channels;
            m_sampleRate = sampleRate;
            m_totalPCMFrameCount = totalPCMFrameCount;
            m_pSampleData = pSampleData;

        m_mut.unlock();
        drwav_free(oldData,nullptr);
        updatePeaks();  
        
        return true;
    }
    struct SamplePeaks
    {
        float minpeak = 0.0f;
        float maxpeak = 0.0f;
    };
    std::vector<std::vector<SamplePeaks>> peaksData;
    DrWavSource()
    {
        
#ifdef __APPLE__
        std::string filename("/Users/teemu/AudioProjects/sourcesamples/db_guit01.wav");
#else
        std::string filename("C:\\MusicAudio\\sourcesamples\\windchimes_c1.wav");
#endif
        importFile(filename);
    }
    void putIntoBuffer(float* dest, int frames, int channels, int startInSource) override
    {
        std::lock_guard<std::mutex> locker(m_mut);
        if (m_channels==0)
        {
            for (int i=0;i<frames*channels;++i)
                dest[i]=0.0f;
            return;
        }
        const int srcchanmap[4][4]=
        {
            {0,0,0,0},
            {0,1,0,1},
            {0,1,2,0},
            {0,1,2,3}
        };
        for (int i=0;i<frames;++i)
        {
            int index = i+startInSource;
            if (index>=0 && index<m_totalPCMFrameCount)
            {
                for (int j=0;j<channels;++j)
                {
                    int actsrcchan = srcchanmap[m_channels-1][j];
                    dest[i*channels+j] = m_pSampleData[index*m_channels+actsrcchan];
                }
            } else
            {
                for (int j=0;j<channels;++j)
                {
                    dest[i*channels+j] = 0.0f;
                }
            }
        }
    }
    ~DrWavSource()
    {
        drwav_free(m_pSampleData,nullptr);
    }
    int getSourceNumChannels() override
    {
        return m_channels;
    }
};

class GrainEngine
{
public:
    GrainEngine()
    {

    }
    void process(float sr,float* buf, float playrate, float pitch, 
        float loopstart, float looplen, float posrand, float grate)
    {
        buf[0] = 0.0f;
        buf[1] = 0.0f;
        buf[2] = 0.0f;
        buf[3] = 0.0f;
        m_gm.m_sr = sr;
        m_gm.m_inputdur = m_src.m_totalPCMFrameCount;
        m_gm.m_loopstart = loopstart;
        m_gm.m_looplen = looplen;
        m_gm.m_sourcePlaySpeed = playrate;
        m_gm.m_pitch = pitch;
        m_gm.m_posrandamt = posrand;
        m_gm.setDensity(grate);
        m_gm.processAudio(buf);
    }
    DrWavSource m_src;
    GrainMixer m_gm{&m_src};
private:
    
};

class XGranularModule : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_PLAYRATE,
        PAR_PITCH,
        PAR_LOOPSTART,
        PAR_LOOPLEN,
        PAR_ATTN_PLAYRATE,
        PAR_ATTN_PITCH,
        PAR_SRCPOSRANDOM,
        PAR_ATTN_LOOPSTART,
        PAR_ATTN_LOOPLEN,
        PAR_GRAINDENSITY,
        PAR_LAST
    };
    enum OUTPUTS
    {
        OUT_AUDIO,
        OUT_LAST
    };
    enum INPUTS
    {
        IN_CV_PLAYRATE,
        IN_CV_PITCH,
        IN_CV_LOOPSTART,
        IN_CV_LOOPLEN,
        IN_LAST
    };
    XGranularModule()
    {
        std::string audioDir = rack::asset::user("XenakiosGrainAudioFiles");
        rack::system::createDirectory(audioDir);
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_PLAYRATE,-2.0f,2.0f,1.0f,"Playrate");
        configParam(PAR_PITCH,-24.0f,24.0f,0.0f,"Pitch");
        configParam(PAR_LOOPSTART,0.0f,1.0f,0.0f,"Loop start");
        configParam(PAR_LOOPLEN,0.0f,1.0f,1.0f,"Loop length");
        configParam(PAR_ATTN_PLAYRATE,-1.0f,1.0f,0.0f,"Playrate CV ATTN");
        configParam(PAR_ATTN_PITCH,-1.0f,1.0f,0.0f,"Pitch CV ATTN");
        configParam(PAR_SRCPOSRANDOM,0.0f,1.0f,0.0f,"Source position randomization");
        configParam(PAR_ATTN_LOOPSTART,-1.0f,1.0f,0.0f,"Loop start CV ATTN");
        configParam(PAR_ATTN_LOOPLEN,-1.0f,1.0f,0.0f,"Loop len CV ATTN");
        configParam(PAR_GRAINDENSITY,0.0f,1.0f,0.25f,"Grain rate");
    }
    json_t* dataToJson() override
    {
        json_t* resultJ = json_object();
        json_object_set(resultJ,"importedfile",json_string(m_currentFile.c_str()));
        return resultJ;
    }
    void dataFromJson(json_t* root) override
    {
        json_t* filenameJ = json_object_get(root,"importedfile");
        if (filenameJ)
        {
            std::string filename(json_string_value(filenameJ));
            importFile(filename);
        }
    }
    void importFile(std::string filename)
    {
        if (filename.size()==0)
            return;
        if (m_eng.m_src.importFile(filename))
        {
            m_currentFile = filename;
        }
    }
    std::string m_currentFile;
    void process(const ProcessArgs& args) override
    {
        float buf[4] ={0.0f,0.0f,0.0f,0.0f};
        float prate = params[PAR_PLAYRATE].getValue();
        prate += inputs[IN_CV_PLAYRATE].getVoltage()*params[PAR_ATTN_PLAYRATE].getValue()/10.0f;
        prate = clamp(prate,-2.0f,2.0f);
        float pitch = params[PAR_PITCH].getValue();
        pitch += inputs[IN_CV_PITCH].getVoltage()*12.0f;
        pitch = clamp(pitch,-24.0f,24.0f);
        float loopstart = params[PAR_LOOPSTART].getValue();
        loopstart += inputs[IN_CV_LOOPSTART].getVoltage()*params[PAR_ATTN_LOOPSTART].getValue()/10.0f;
        loopstart = clamp(loopstart,0.0f,1.0f);
        float looplen = params[PAR_LOOPLEN].getValue();
        looplen += inputs[IN_CV_LOOPLEN].getVoltage()*params[PAR_ATTN_LOOPLEN].getValue()/10.0f;
        looplen = clamp(looplen,0.0f,1.0f);
        float posrnd = params[PAR_SRCPOSRANDOM].getValue();
        float grate = params[PAR_GRAINDENSITY].getValue();
        grate = 0.01f+std::pow(grate,2.0f)*0.49;
        m_eng.process(args.sampleRate, buf,prate,pitch,loopstart,looplen,posrnd,grate);
        outputs[OUT_AUDIO].setVoltage(buf[0]*5.0f);
        graindebugcounter = m_eng.m_gm.debugCounter;
    }
    int graindebugcounter = 0;
    GrainEngine m_eng;
private:
    
};

struct LoadFileItem : MenuItem
{
    XGranularModule* m_mod = nullptr;
    void onAction(const event::Action &e) override
    {
        std::string dir = asset::plugin(pluginInstance, "/res");
        osdialog_filters* filters = osdialog_filters_parse("WAV file:wav");
        char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
        osdialog_filters_free(filters);
        if (!pathC) {
            return;
        }
        std::string path = pathC;
        std::free(pathC);
        m_mod->importFile(path);
    }
};

class XGranularWidget : public rack::ModuleWidget
{
public:
    XGranularModule* m_gm = nullptr;
    
	void appendContextMenu(Menu *menu) override 
    {
		auto loadItem = createMenuItem<LoadFileItem>("Import .wav file...");
		loadItem->m_mod = m_gm;
		menu->addChild(loadItem);
        auto normItem = createMenuItem([this](){  m_gm->m_eng.m_src.normalize(1.0f); },"Normalize buffer");
        menu->addChild(normItem);
        auto revItem = createMenuItem([this](){  m_gm->m_eng.m_src.reverse(); },"Reverse buffer");
        menu->addChild(revItem);
    }
    XGranularWidget(XGranularModule* m)
    {
        setModule(m);
        m_gm = m;
        box.size.x = 200;
        addChild(new LabelWidget({{1,6},{box.size.x,1}}, "GRAINS",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        PortWithBackGround<PJ301MPort>* port = nullptr;
        addOutput(port = createOutput<PortWithBackGround<PJ301MPort>>(Vec(1, 34), m, XGranularModule::OUT_AUDIO));
        port->m_text = "AUDIO OUT";
        addChild(new KnobInAttnWidget(this,
            "PLAYRATE",XGranularModule::PAR_PLAYRATE,
            XGranularModule::IN_CV_PLAYRATE,XGranularModule::PAR_ATTN_PLAYRATE,1,60));
        addChild(new KnobInAttnWidget(this,
            "PITCH",XGranularModule::PAR_PITCH,XGranularModule::IN_CV_PITCH,-1,82,60));
        addChild(new KnobInAttnWidget(this,"LOOP START",
            XGranularModule::PAR_LOOPSTART,XGranularModule::IN_CV_LOOPSTART,XGranularModule::PAR_ATTN_LOOPSTART,1,101));
        addChild(new KnobInAttnWidget(this,"LOOP LENGTH",
            XGranularModule::PAR_LOOPLEN,XGranularModule::IN_CV_LOOPLEN,XGranularModule::PAR_ATTN_LOOPLEN,82,101));
        addChild(new KnobInAttnWidget(this,"SOURCE POS RAND",XGranularModule::PAR_SRCPOSRANDOM,-1,-1,1,142));
        addChild(new KnobInAttnWidget(this,"GRAIN RATE",XGranularModule::PAR_GRAINDENSITY,-1,-1,82,142));
    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x50, 0x50, 0x50, 0xff));
        nvgRect(args.vg,0.0f,0.0f,box.size.x,box.size.y);
        nvgFill(args.vg);
        if (m_gm)
        {
            char buf[100];
            sprintf(buf,"%d",m_gm->graindebugcounter);
            nvgFontSize(args.vg, 15);
            nvgFontFaceId(args.vg, getDefaultFont(0)->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            
            nvgText(args.vg, 1 , 230, buf, NULL);

            nvgStrokeColor(args.vg,nvgRGBA(0xff, 0xff, 0xff, 0xff));
            auto& src = m_gm->m_eng.m_src;
            int numpeaks = box.size.x - 2;
            int numchans = src.m_channels;
            float numsrcpeaks = src.peaksData[0].size();
            float chanh = 100.0/numchans;
            nvgBeginPath(args.vg);
            for (int i=0;i<numchans;++i)
            {
                for (int j=0;j<numpeaks;++j)
                {
                    float index = rescale(j,0,numpeaks,0.0f,numsrcpeaks-1.0f);
                    if (index<numsrcpeaks)
                    {
                        int index_i = index;
                        float minp = src.peaksData[i][index_i].minpeak;
                        float maxp = src.peaksData[i][index_i].maxpeak;
                        float ycor0 = rescale(minp,-1.0f,1.0,0.0f,chanh);
                        float ycor1 = rescale(maxp,-1.0f,1.0,0.0f,chanh);
                        nvgMoveTo(args.vg,j,250.0+chanh*i+ycor0);
                        nvgLineTo(args.vg,j,250.0+chanh*i+ycor1);
                    }
                    
                }
            }
            nvgStroke(args.vg);
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0x80));
            float loopstart = m_gm->m_eng.m_gm.m_actLoopstart;
            float loopend = m_gm->m_eng.m_gm.m_actLoopend;
            float loopw = rescale(loopend-loopstart,0.0f,1.0f,0.0f,box.size.x-2.0f);
            float xcor = rescale(loopstart,0.0f,1.0f,0.0f,box.size.x-2.0f);
            nvgRect(args.vg,xcor,250.0f,loopw,100.0f);
            nvgFill(args.vg);
            nvgBeginPath(args.vg);
            nvgStrokeColor(args.vg,nvgRGBA(0xff, 0xff, 0xff, 0xff));
            float ppos = m_gm->m_eng.m_gm.m_actSourcePos;
            float srcdur = m_gm->m_eng.m_gm.m_inputdur;
            xcor = rescale(ppos,0.0f,srcdur,0.0f,box.size.x-2.0f);
            nvgMoveTo(args.vg,xcor,250.0f);
            nvgLineTo(args.vg,xcor,250.0+100.0f);
            nvgStroke(args.vg);
        }
        


        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelXGranular = createModel<XGranularModule, XGranularWidget>("XGranular");
