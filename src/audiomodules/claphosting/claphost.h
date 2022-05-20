#pragma once
#include <iostream>
#include <clap/entry.h>
#include <clap/host.h>
#include <clap/events.h>
#include <clap/plugin-factory.h>
#include <clap/ext/params.h>
#include <clap/ext/audio-ports.h>
#include <dlfcn.h>
#include <vector>
#include <unordered_map>
#include "mischelpers.h"

inline clap_plugin_entry_t *entryFromClapPath(const std::string &p)
{
    void    *handle;
    int     *iptr;

    handle = dlopen(p.c_str(), RTLD_LOCAL | RTLD_LAZY);

    iptr = (int *)dlsym(handle, "clap_entry");

    return (clap_plugin_entry_t *)iptr;
}

class clap_processor
{
public:
    bool m_inited = false;
    const clap_plugin_t* m_plug = nullptr;
    clap_plugin_entry_t* m_entry = nullptr;
    clap_processor();
    
    ~clap_processor()
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
    void prepare(int inchans, int outchans, int maxblocksize, float samplerate);
    void processAudio(float* buf, int nframes);
    void setParameter(int id, float v);
    void incDecParameter(int index, float step);
private:
    std::vector<std::vector<float>> m_in_bufs;
    std::vector<float*> m_in_buf_ptrs;
    std::vector<clap_audio_buffer_t> m_clap_in_ports;
    std::vector<std::vector<float>> m_out_bufs;
    std::vector<float*> m_out_buf_ptrs;
    std::vector<clap_audio_buffer_t> m_clap_out_ports;
    clap_input_events_t m_in_events;
    clap_output_events_t m_out_events;
    bool isStarted = false;
    std::unordered_map<uint32_t, clap_param_info> paramInfo;
    std::vector<uint32_t> orderedParamIds;
    spinlock m_spinlock;
};
