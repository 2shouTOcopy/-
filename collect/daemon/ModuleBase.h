#pragma once

#include "ModuleDef.h"

class CAbstractUserModule
{
public:
    virtual ~CAbstractUserModule() {}

    virtual int config(const std::string &key, const std::string &val) { return 0; }

    virtual int complex_config() { return 0; }

    virtual int init() { return 0; }

    virtual int read() { return 0; }

    virtual int complex_read() { return 0; }

    virtual int write(const data_set_t *ds, const value_list_t *vl) { return 0; }

    virtual int flush() { return 0; }

    virtual int missing() { return 0; }

    virtual int cache_event() { return 0; }

    virtual int shutdown() { return 0; }

    virtual int data_set() { return 0; }

    virtual int logmsg() { return 0; }

    virtual int notification() { return 0; }
};

