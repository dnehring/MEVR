#include "XrProbe.hpp"

#include <openxr/openxr.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace mevr::setup {

HeadsetProbe ProbeHeadset() {
    HeadsetProbe probe;
    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(create.applicationInfo.applicationName, "MEVR Setup", XR_MAX_APPLICATION_NAME_SIZE - 1);
    create.applicationInfo.applicationVersion = 1;
    std::strncpy(create.applicationInfo.engineName, "MEVR", XR_MAX_ENGINE_NAME_SIZE - 1);
    create.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    XrInstance instance = XR_NULL_HANDLE;
    XrResult result = xrCreateInstance(&create, &instance);
    if (XR_FAILED(result)) {
        probe.error = "no OpenXR runtime answered (" + std::to_string(static_cast<int>(result)) + ")";
        return probe;
    }
    XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &instanceProperties))) {
        probe.runtime = instanceProperties.runtimeName;
    }
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    result = xrGetSystem(instance, &systemInfo, &system);
    if (XR_FAILED(result)) {
        probe.error = result == XR_ERROR_FORM_FACTOR_UNAVAILABLE ? "the headset isn't connected or awake" : "no headset (" + std::to_string(static_cast<int>(result)) + ")";
        xrDestroyInstance(instance);
        return probe;
    }
    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystemProperties(instance, system, &systemProperties))) {
        probe.system = systemProperties.systemName;
    }
    std::uint32_t count = 0;
    xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr);
    std::vector<XrViewConfigurationView> views(count, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (count > 0 && XR_SUCCEEDED(xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, views.data()))) {
        for (XrViewConfigurationView const& v : views) {
            probe.width = std::max(probe.width, static_cast<int>(v.recommendedImageRectWidth));
            probe.height = std::max(probe.height, static_cast<int>(v.recommendedImageRectHeight));
        }
        probe.ok = probe.width > 0 && probe.height > 0;
    }
    if (!probe.ok) {
        probe.error = "the runtime gave no eye size";
    }
    xrDestroyInstance(instance);
    return probe;
}

} // namespace mevr::setup
