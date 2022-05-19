#pragma once
#include <iostream>
#include <clap/entry.h>
#include <clap/host.h>
#include <clap/events.h>
#include <clap/plugin-factory.h>
#include <clap/ext/params.h>
#include <clap/ext/audio-ports.h>
#include <dlfcn.h>

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
    void prepare(int inchans, int outchans, int maxblocksize, float samplerate)
    {

    }
};
