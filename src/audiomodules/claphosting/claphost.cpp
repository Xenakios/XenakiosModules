#include "claphost.h"

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

clap_processor::clap_processor()
{
    std::cout << "initing clap host processor...\n";
    std::string cpath = "/home/teemu/codestuff/surge/build/surge_xt_products/Surge XT Effects.clap";
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

    std::cout << "Plugin description: \n"
            << "   name     : " << desc->name << "\n"
            << "   version  : " << desc->version << "\n"
            << "   id       : " << desc->id << "\n"
            << "   desc     : " << desc->description << "\n";
    //      << "   features : ";
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
            //aud.paramInfo[inf.id] = inf;

            double d;
            inst_param->get_value(m_plug, inf.id, &d);
            //aud.initialParamValues[inf.id] = d;

            std::cout << i << " " << inf.module << " " << inf.name << " (id=0x" << std::hex
                      << inf.id << std::dec << ") val=" << d << std::endl;
        }
    }
    else
    {
        std::cout << "No Parameters Available" << std::endl;
    }
    m_inited = true;
}