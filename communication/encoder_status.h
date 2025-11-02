#pragma once
#include "output_mode.h"

extern "C" {
    // 你们已有的 C 接口
    int getEncoderStatus(void);
}

class EncoderStatusAdapter : public IEncoderStatusProvider {
public:
    int getStatus() override { return getEncoderStatus(); }
};
