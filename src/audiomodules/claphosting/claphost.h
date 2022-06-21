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
#include <functional>
#include "choc_SingleReaderSingleWriterFIFO.h"
#include "jansson.h"

inline bool findStringIC(const std::string & strSource, const std::string & strToFind)
{
  auto it = std::search(
    strSource.begin(), strSource.end(),
    strToFind.begin(),   strToFind.end(),
    [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
  );
  return (it != strSource.end() );
}

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
    
    ~clap_processor();
    
    void prepare(int inchans, int outchans, int maxblocksize, float samplerate);
    void processAudio(float* buf, int nframes);
    float getParameter(int id);
    void setParameter(int id, float v);
    void incDecParameter(int index, float step);
    std::string getParameterValueFormatted(int index);
    std::string getParameterName(int index);
    json_t* dataToJson();
    std::string dataFromJson(json_t* j);
    choc::fifo::SingleReaderSingleWriterFIFO<std::function<void(void)>>* exFIFO = nullptr;
private:
    std::vector<std::vector<float>> m_in_bufs;
    std::vector<float*> m_in_buf_ptrs;
    std::vector<clap_audio_buffer_t> m_clap_in_ports;
    std::vector<std::vector<float>> m_out_bufs;
    std::vector<float*> m_out_buf_ptrs;
    std::vector<clap_audio_buffer_t> m_clap_out_ports;
    clap_input_events_t m_in_events;
    clap_output_events_t m_out_events;
    std::atomic<bool> isStarted{false};
    std::unordered_map<uint32_t, clap_param_info> paramInfo;
    std::vector<uint32_t> orderedParamIds;
    spinlock m_spinlock;
    bool m_is_surge_fx = false;
};
