#pragma once

#include <vector>

extern "C" {
#include <va/va.h>
}

#include "context.h"

struct Buffer;
struct RequestData;
struct Surface;
class V4L2M2MDevice;

class VP9Context : public Context {
public:
    static std::vector<VAProfile> supported_profiles(const V4L2M2MDevice& device);

    VP9Context(RequestData* driver_data, VAConfigID config_id, int picture_width, int picture_height,
        std::span<VASurfaceID> surface_ids)
        : Context(driver_data, config_id, picture_width, picture_height, surface_ids) {};
    VAStatus store_buffer(const Buffer& buffer) const override;
    int set_controls() override;
};
