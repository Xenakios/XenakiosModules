#include "claphost.h"
#include <cstring>
#include "portaudio.h"

const void *get_extension(const struct clap_host *host, const char *eid)
{
    std::cout << "Requesting Extension " << eid << std::endl;
    return nullptr;
}

void request_restart(const struct clap_host *h) {}

void request_process(const struct clap_host *h) {}

void request_callback(const struct clap_host *h) {}

static clap_host xen_host_static{
    CLAP_VERSION_INIT, nullptr,       "GRLOOPERHOST",     "Xenakios", "no website",
    "0.0.0",           get_extension, request_restart, request_process,     request_callback};

#define USE_CHOC_RB 0

#if !USE_CHOC_RB
struct micro_input_events
{
    static constexpr int max_evt_size = 10 * 1024;
    static constexpr int max_events = 4096;
    uint8_t data[max_evt_size * max_events];
    uint32_t sz{0};

    static void setup(clap_input_events *evt)
    {
        evt->ctx = new micro_input_events();
        evt->size = size;
        evt->get = get;
    }
    static void destroy(clap_input_events *evt) { delete (micro_input_events *)evt->ctx; }

    static uint32_t size(const clap_input_events *e)
    {
        auto mie = static_cast<micro_input_events *>(e->ctx);
        return mie->sz;
    }

    static const clap_event_header_t *get(const clap_input_events *e, uint32_t index)
    {
        auto mie = static_cast<micro_input_events *>(e->ctx);
        //assert(index >= 0);
        //assert(index < max_events);
        uint8_t *ptr = &(mie->data[index * max_evt_size]);
        return reinterpret_cast<clap_event_header_t *>(ptr);
    }

    template <typename T> static void push(clap_input_events *e, const T &t)
    {
        auto mie = static_cast<micro_input_events *>(e->ctx);
        //assert(t.header.size <= max_evt_size);
        //assert(mie->sz < max_events - 1);
        uint8_t *ptr = &(mie->data[mie->sz * max_evt_size]);
        memcpy(ptr, &t, t.header.size);
        mie->sz++;
    }

    static void reset(clap_input_events *e)
    {
        auto mie = static_cast<micro_input_events *>(e->ctx);
        mie->sz = 0;
    }
};
#else
struct micro_input_events
{
    static constexpr int max_evt_size = 10 * 1024;
    static constexpr int max_events = 4096;
    uint8_t data[max_evt_size * max_events];
    uint32_t sz{0};
    choc::fifo::SingleReaderSingleWriterFIFO<uint8_t> fifo;
    static void setup(clap_input_events *evt)
    {
        auto mie = new micro_input_events;
        mie->fifo.reset(16384);
        evt->ctx = mie;
        evt->size = size;
        evt->get = get;
    }
    static void destroy(clap_input_events *evt) { delete (micro_input_events *)evt->ctx; }

    static uint32_t size(const clap_input_events *e)
    {
        auto mie = static_cast<micro_input_events *>(e->ctx);
        return mie->fifo.getUsedSlots();
    }

    static const clap_event_header_t *get(const clap_input_events *e, uint32_t index)
    {
        auto mie = static_cast<micro_input_events *>(e->ctx);
        //assert(index >= 0);
        //assert(index < max_events);
        uint8_t *ptr = &(mie->data[index * max_evt_size]);
        return reinterpret_cast<clap_event_header_t *>(ptr);
    }

    template <typename T> static void push(clap_input_events *e, const T &t)
    {
        auto mie = static_cast<micro_input_events *>(e->ctx);
        //assert(t.header.size <= max_evt_size);
        //assert(mie->sz < max_events - 1);
        uint8_t *ptr = &(mie->data[mie->sz * max_evt_size]);
        memcpy(ptr, &t, t.header.size);
        mie->sz++;
    }

    static void reset(clap_input_events *e)
    {
        auto mie = static_cast<micro_input_events *>(e->ctx);
        mie->sz = 0;
    }
};
#endif
struct micro_output_events
{
    static constexpr int max_evt_size = 1024;
    static constexpr int max_events = 4096;
    uint8_t data[max_evt_size * max_events];
    uint32_t sz{0};

    static void setup(clap_output_events *evt)
    {
        evt->ctx = new micro_output_events();
        evt->try_push = try_push;
    }
    static void destroy(clap_output_events *evt) { delete (micro_output_events *)evt->ctx; }

    static bool try_push(const struct clap_output_events *list, const clap_event_header_t *event)
    {
        auto mie = static_cast<micro_output_events *>(list->ctx);
        if (mie->sz >= max_events || event->size >= max_evt_size)
            return false;

        uint8_t *ptr = &(mie->data[mie->sz * max_evt_size]);
        memcpy(ptr, event, event->size);
        mie->sz++;
        return true;
    }

    static uint32_t size(clap_output_events *e)
    {
        auto mie = static_cast<micro_output_events *>(e->ctx);
        return mie->sz;
    }

    static void reset(clap_output_events *e)
    {
        auto mie = static_cast<micro_output_events *>(e->ctx);
        mie->sz = 0;
    }
};

clap_processor::clap_processor()
{
    std::cout << "initing clap host processor...\n";
    std::string cpath = "/home/pi/.clap/Surge XT Effects.clap";
    auto entry = entryFromClapPath(cpath);

    if (!entry)
    {
        std::cout << "Got a null clap entrypoint\n";
        return;
    }
    
    m_entry = entry;
    auto version = m_entry->clap_version;
    std::cout << "Clap Version        : " << version.major << "." << version.minor << "."
            << version.revision << std::endl;

    m_entry->init(cpath.c_str());
    auto fac = (clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    auto plugin_count = fac->get_plugin_count(fac);
    if (plugin_count<=0)
    {
        std::cout << "no plugins to manufacture\n";
        return;
    }
    
    auto desc = fac->get_plugin_descriptor(fac, 0);
    std::string descname(desc->name);
    if (findStringIC(descname,"surge xt effects"))
    {
        std::cout << "plugin is surge fx\n";
        m_is_surge_fx = true;
    }
    /*
    std::cout << "Plugin description: \n"
            << "   name     : " << desc->name << "\n"
            << "   version  : " << desc->version << "\n"
            << "   id       : " << desc->id << "\n"
            << "   desc     : " << desc->description << "\n";
    //      << "   features : ";
    */
    m_plug = fac->create_plugin(fac, &xen_host_static, desc->id);
    m_plug->init(m_plug);
    m_plug->activate(m_plug, 44100, 512, 512);
    auto inst_param = (clap_plugin_params_t *)m_plug->get_extension(m_plug, CLAP_EXT_PARAMS);
    if (inst_param)
    {
        auto pc = inst_param->count(m_plug);
        std::cout << "Plugin has " << pc << " params " << std::endl;

        for (auto i = 0U; i < pc; ++i)
        {
            clap_param_info_t inf;
            inst_param->get_info(m_plug, i, &inf);
            paramInfo[inf.id] = inf;
            //aud.paramInfo[inf.id] = inf;
            orderedParamIds.push_back(inf.id);
            double d;
            inst_param->get_value(m_plug, inf.id, &d);
            
            //aud.initialParamValues[inf.id] = d;

            //std::cout << i << " " << inf.module << " " << inf.name << " (id=0x" << std::hex
            //          << inf.id << std::dec << ") val=" << d << std::endl;
        }
    }
    else
    {
        std::cout << "No Parameters Available" << std::endl;
    }
    m_clap_in_ports.resize(1);
    m_clap_out_ports.resize(1);
    m_in_bufs.resize(4);
    m_in_buf_ptrs.resize(4);
    m_out_bufs.resize(2);
    m_out_buf_ptrs.resize(2);
    for (int i=0;i<m_in_bufs.size();++i)
    {
        m_in_bufs[i].resize(4096);
        m_in_buf_ptrs[i] = m_in_bufs[i].data();
    }
    for (int i=0;i<m_out_bufs.size();++i)
    {
        m_out_bufs[i].resize(4096);
        m_out_buf_ptrs[i] = m_out_bufs[i].data();
    }
        
    m_clap_in_ports[0].channel_count = 4;
    m_clap_in_ports[0].constant_mask = 0;
    m_clap_in_ports[0].latency = 0;
    m_clap_in_ports[0].data64 = nullptr;
    m_clap_in_ports[0].data32 = m_in_buf_ptrs.data();
    
    m_clap_out_ports[0].channel_count = 2;
    m_clap_out_ports[0].constant_mask = 0;
    m_clap_out_ports[0].latency = 0;
    m_clap_out_ports[0].data64 = nullptr;
    m_clap_out_ports[0].data32 = m_out_buf_ptrs.data();
    
    micro_input_events::setup(&m_in_events);
    micro_output_events::setup(&m_out_events);

    m_inited = true;

}

clap_processor::~clap_processor()
{
    if (m_plug)
    {
        m_plug->deactivate(m_plug);
        m_plug->destroy(m_plug);
    }
    if (m_entry)
    {
        m_entry->deinit();
    }
    
}

json_t* clap_processor::dataToJson()
{
    json_t* result = json_object();
    json_t* paramsj = json_array();
    auto inst_param = (clap_plugin_params_t *)m_plug->get_extension(m_plug, CLAP_EXT_PARAMS);
    for (int i=0;i<inst_param->count(m_plug);++i)
    {
        auto parid = orderedParamIds[i];
        double theval = 0.0;
        inst_param->get_value(m_plug,parid,&theval);    
        json_array_append(paramsj,json_real(theval));
    }
    json_object_set(result,"params",paramsj);
    return result;
}

std::string clap_processor::dataFromJson(json_t* j)
{
    if (!j)
        return "no plugin data";
    json_t* paramsj = json_object_get(j,"params");
    if (!paramsj)
        return "no params data";
    while (!isStarted) 
    {
        Pa_Sleep(10);
    } // awful hack...
    int numparsstored = json_array_size(paramsj);
    auto inst_param = (clap_plugin_params_t *)m_plug->get_extension(m_plug, CLAP_EXT_PARAMS);
    int numcurpars = inst_param->count(m_plug);
    for (int i=0;i<numparsstored;++i)
    {
        if (i<numcurpars)
        {
            float val = json_real_value(json_array_get(paramsj,i));
            setParameter(i,val);
        }
    }
    return "";
}

void clap_processor::setParameter(int id, float v)
{
    if (id>=0 && id < orderedParamIds.size())
    {
        const auto& parinfo = paramInfo[orderedParamIds[id]];
        auto valset = clap_event_param_value();
        valset.header.size = sizeof(clap_event_param_value);
        valset.header.type = (uint16_t)CLAP_EVENT_PARAM_VALUE;
        valset.header.time = 0;
        valset.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        valset.header.flags = 0;
        valset.param_id = parinfo.id;
        valset.note_id = -1;
        valset.port_index = -1;
        valset.channel = -1;
        valset.key = -1;
        valset.value = v;
        valset.cookie = parinfo.cookie;
        m_spinlock.lock();
        micro_input_events::push(&m_in_events, valset);
        m_spinlock.unlock();
    }
}

float clap_processor::getParameter(int index)
{
    if (index>=0 && index<orderedParamIds.size())
    {
        auto inst_param = (clap_plugin_params_t *)m_plug->get_extension(m_plug, CLAP_EXT_PARAMS);
        auto parid = orderedParamIds[index];
        double oldval = 0.0;
        inst_param->get_value(m_plug,parid,&oldval);
        return oldval;
    }
    return 0.0f;
}

std::string clap_processor::getParameterValueFormatted(int index)
{
    if (index>=0 && index<orderedParamIds.size())
    {
        auto inst_param = (clap_plugin_params_t *)m_plug->get_extension(m_plug, CLAP_EXT_PARAMS);
        auto parid = orderedParamIds[index];
        double oldval = 0.0;
        if (inst_param->get_value(m_plug,parid,&oldval))
        {
            char txtbuf[256];
            memset(txtbuf,0,256);
            if (inst_param->value_to_text(m_plug,parid,oldval,txtbuf,256))
                return txtbuf;
            else return "err1";
        } else
            return "err0";
        
    }
    return "N/A";
}

std::string clap_processor::getParameterName(int index)
{
    if (index>=0 && index<orderedParamIds.size())
    {
        auto inst_param = (clap_plugin_params_t *)m_plug->get_extension(m_plug, CLAP_EXT_PARAMS);
        clap_param_info_t parinfo;
        if (inst_param->get_info(m_plug,index,&parinfo))
        {
            return parinfo.name;// paramInfo[parid].name;
        } else return "Error";
        
        //auto parid = orderedParamIds[index];
        
    }
    return "N/A";
}

void clap_processor::incDecParameter(int index, float step)
{
    if (index>=0 && index<orderedParamIds.size())
    {
        auto inst_param = (clap_plugin_params_t *)m_plug->get_extension(m_plug, CLAP_EXT_PARAMS);
        auto parid = orderedParamIds[index];
        double minval = paramInfo[parid].min_value;
        double maxval = paramInfo[parid].max_value;
        double range = maxval - minval;
        double oldval = 0.0;
        float pardelta = 0.01f;
        if (m_is_surge_fx && index == 12) // surge fx fx type
        {
            //oldval = std::floor(1.0*27)/27;
            pardelta = 1.0/27;
        }    
        if (inst_param->get_value(m_plug,parid,&oldval))
        {
            oldval += step * range * pardelta;
            if (oldval<minval)
                oldval = minval;
            if (oldval>maxval)
                oldval = maxval;
            //std::cout << "set parameter " << paramInfo[parid].name << " relatively to " << oldval << "\n";
            
            //exFIFO->push([this,parid,oldval,inst_param,index]()
            //{
                //std::cout << "set parameter " << paramInfo[parid].name << " relatively to " << oldval << "\n";
                //char txtbuf[256];
                //memset(txtbuf,0,256);
                //inst_param->value_to_text(m_plug,parid,oldval,txtbuf,256);
                //std::cout << txtbuf << "\n";
            //});
            
            setParameter(index,oldval);
        }
    }
    
}

void clap_processor::prepare(int inchans, int outchans, int maxblocksize, float samplerate)
{

}

void clap_processor::processAudio(float* buf, int nframes)
{
    m_spinlock.lock();
    if (!isStarted)
    {
        m_plug->start_processing(m_plug);
        isStarted = true;
    }
    for (int i=0;i<nframes;++i)
    {
        m_in_bufs[0][i] = buf[i*2+0];
        m_in_bufs[1][i] = buf[i*2+1];
        m_in_bufs[2][i] = 0.0f;
        m_in_bufs[3][i] = 0.0f;
    }
    clap_process_t process;
    process.steady_time = -1;
    process.frames_count = nframes;
    process.transport = nullptr; // we do need to fix this

    process.audio_inputs = m_clap_in_ports.data();
    process.audio_inputs_count = 1;
    process.audio_outputs = m_clap_out_ports.data();
    process.audio_outputs_count = 1;

    process.in_events = &m_in_events;
    process.out_events = &m_out_events;

    auto res = m_plug->process(m_plug, &process);
    micro_output_events::reset(&m_out_events);
    micro_input_events::reset(&m_in_events);
    for (int i=0;i<nframes;++i)
    {
        buf[i*2+0] = m_out_bufs[0][i];
        buf[i*2+1] = m_out_bufs[1][i];
    }
    m_spinlock.unlock();   
}

std::vector<clap_info_item> clap_processor::scanClaps()
{
    std::vector<clap_info_item> results;
    std::system("clap-info -s > clap-infos.json");
    json_error_t err{};
    auto json = json_load_file("clap-infos.json",0,&err);
    if (json)
    {
        auto clapsarrj = json_object_get(json,"clap-files");
        if (clapsarrj)
        {
            int numfiles = json_array_size(clapsarrj);
            for (int i=0;i<numfiles;++i)
            {
                auto plugjson = json_array_get(clapsarrj,i);
                auto plugpathjson = json_object_get(plugjson,"path");
                if (plugpathjson)
                {
                    std::string temp(json_string_value(plugpathjson));
                    auto pluginsarrjson = json_object_get(plugjson,"plugins");
                    int numsubplugs = json_array_size(pluginsarrjson);
                    for (int i=0;i<numsubplugs;++i)
                    {
                        auto subplugjson = json_array_get(pluginsarrjson,i);
                        auto plugnamejson = json_object_get(subplugjson,"name");
                        clap_info_item item;
                        item.clap_filepath = temp;
                        item.clap_plugname = json_string_value(plugnamejson);
                        item.sub_plugin_index = i;
                        results.push_back(item);
                        std::cout << temp << " : " << item.clap_plugname << "\n";
                    }
                }
            }
        }
        json_decref(json);
    } else
    {
        std::cout << err.text << "\n";
    }
    return results;
}