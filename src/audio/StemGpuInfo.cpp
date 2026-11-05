#include "StemGpuInfo.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <dxgi1_6.h>
#endif

juce::String StemGpuInfo::describe() const
{
    if (! valid)
        return "GPU: unknown";

    return "GPU: " + name + ", " + juce::String (dedicatedMiB) + " MB, driver " + driverVersion
         + ", compute preemption " + computePreemption;
}

#if JUCE_WINDOWS

namespace
{
template <typename T>
struct ComRelease
{
    T* p = nullptr;
    ~ComRelease() { if (p != nullptr) p->Release(); }
    T** put() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

juce::String preemptionName (DXGI_COMPUTE_PREEMPTION_GRANULARITY g)
{
    switch (g)
    {
        case DXGI_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY:   return "DMA_BUFFER";
        case DXGI_COMPUTE_PREEMPTION_DISPATCH_BOUNDARY:     return "DISPATCH";
        case DXGI_COMPUTE_PREEMPTION_THREAD_GROUP_BOUNDARY: return "THREAD_GROUP";
        case DXGI_COMPUTE_PREEMPTION_THREAD_BOUNDARY:       return "THREAD";
        case DXGI_COMPUTE_PREEMPTION_INSTRUCTION_BOUNDARY:  return "INSTRUCTION";
        default:                                            return juce::String ((int) g);
    }
}

bool fill (IDXGIAdapter1* adapter, StemGpuInfo& info)
{
    ComRelease<IDXGIAdapter2> adapter2;
    if (FAILED (adapter->QueryInterface (__uuidof (IDXGIAdapter2), reinterpret_cast<void**> (adapter2.put()))))
        return false;

    DXGI_ADAPTER_DESC2 desc {};
    if (FAILED (adapter2->GetDesc2 (&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        return false;

    info.name = juce::String (desc.Description).trim();
    info.dedicatedMiB = (juce::int64) (desc.DedicatedVideoMemory / (1024 * 1024));
    info.computePreemption = preemptionName (desc.ComputePreemptionGranularity);

    LARGE_INTEGER umd {};
    if (SUCCEEDED (adapter->CheckInterfaceSupport (__uuidof (IDXGIDevice), &umd)))
        info.driverVersion = juce::String (HIWORD (umd.HighPart)) + "." + juce::String (LOWORD (umd.HighPart)) + "."
                           + juce::String (HIWORD (umd.LowPart)) + "." + juce::String (LOWORD (umd.LowPart));
    info.valid = true;
    return true;
}
} // namespace

StemGpuInfo queryStemGpuInfo()
{
    StemGpuInfo info;
    ComRelease<IDXGIFactory1> factory;
    if (FAILED (CreateDXGIFactory1 (__uuidof (IDXGIFactory1), reinterpret_cast<void**> (factory.put()))))
        return info;

    // Same order DirectML's default adapter choice uses: high-performance first, hardware only.
    ComRelease<IDXGIFactory6> factory6;
    if (SUCCEEDED (factory->QueryInterface (__uuidof (IDXGIFactory6), reinterpret_cast<void**> (factory6.put()))))
    {
        for (UINT i = 0;; ++i)
        {
            ComRelease<IDXGIAdapter1> adapter;
            if (FAILED (factory6->EnumAdapterByGpuPreference (i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                              __uuidof (IDXGIAdapter1), reinterpret_cast<void**> (adapter.put()))))
                break;
            if (fill (adapter.p, info))
                return info;
        }
    }

    for (UINT i = 0;; ++i)
    {
        ComRelease<IDXGIAdapter1> adapter;
        if (FAILED (factory->EnumAdapters1 (i, adapter.put())))
            break;
        if (fill (adapter.p, info))
            return info;
    }
    return info;
}

#else

StemGpuInfo queryStemGpuInfo()
{
    return {};
}

#endif
