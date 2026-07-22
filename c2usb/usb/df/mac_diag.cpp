/// @file
///
/// UHK patch: port-agnostic MAC anomaly diagnostics, see mac_diag.hpp.
///
#include "usb/df/mac_diag.hpp"
#include "usb/df/mac.hpp"

extern "C" __attribute__((weak)) void c2usb_log(const char*, ...) {}
extern "C" __attribute__((weak)) uint32_t c2usb_diag_time_ms(void)
{
    return 0;
}
extern "C" __attribute__((weak)) void c2usb_diag_dump_port(void) {}

using namespace usb::df;

namespace
{
const char* const category_names[diag::CATEGORY_COUNT] = {
    "enqueue fail", "EP error",       "ctrl error",      "ctrl stall",
    "evt dropped",  "EP busy reject", "no-power reject",
};

struct diag_stat
{
    uint32_t total;     // since boot / reset
    uint32_t since_ok;  // since last successful data EP transfer
    uint32_t last_time; // ms
    int last_err;
    uint8_t last_ep;
};

struct diag_bus_entry
{
    uint32_t time;
    uint32_t count; // consecutive events of the same type are collapsed
    diag::bus_event type;
};

constexpr size_t BUS_RING_SIZE = 16;

struct
{
    diag_stat stats[diag::CATEGORY_COUNT];
    diag_bus_entry bus_ring[BUS_RING_SIZE];
    uint8_t bus_ring_next;
    uint32_t last_ok_time;
    uint8_t last_ok_ep;
    uint32_t ok_count;
    mac* mac_;
} state;

const char* bus_name(diag::bus_event type)
{
    switch (type)
    {
    case diag::bus_event::RESET:
        return "reset";
    case diag::bus_event::SUSPEND:
        return "suspend";
    case diag::bus_event::RESUME:
        return "resume";
    case diag::bus_event::SLEEP:
        return "sleep";
    case diag::bus_event::VBUS_ON:
        return "vbus-on";
    case diag::bus_event::VBUS_OFF:
        return "vbus-off";
    case diag::bus_event::ERROR:
        return "error";
    case diag::bus_event::SET_CONFIG:
        return "set-config";
    case diag::bus_event::DECONFIG:
        return "deconfig";
    default:
        return "?";
    }
}

const char* power_name(usb::power::state s)
{
    switch (s)
    {
    case usb::power::state::L0_ON:
        return "L0-on";
    case usb::power::state::L1_SLEEP:
        return "L1-sleep";
    case usb::power::state::L2_SUSPEND:
        return "L2-suspend";
    case usb::power::state::L3_OFF:
        return "L3-off";
    default:
        return "?";
    }
}
} // namespace

namespace usb::df::diag
{
void record(category cat, uint8_t ep, int err)
{
    auto& s = state.stats[cat];
    s.total++;
    s.since_ok++;
    s.last_time = c2usb_diag_time_ms();
    s.last_err = err;
    s.last_ep = ep;
}

void success(uint8_t ep)
{
    state.ok_count++;
    state.last_ok_time = c2usb_diag_time_ms();
    state.last_ok_ep = ep;
    for (auto& s : state.stats)
    {
        s.since_ok = 0;
    }
}

void bus(bus_event event)
{
    auto& last = state.bus_ring[(state.bus_ring_next + BUS_RING_SIZE - 1) % BUS_RING_SIZE];
    if (last.count and (last.type == event))
    {
        last.count++;
        last.time = c2usb_diag_time_ms();
        return;
    }
    state.bus_ring[state.bus_ring_next] = {c2usb_diag_time_ms(), 1, event};
    state.bus_ring_next = (state.bus_ring_next + 1) % BUS_RING_SIZE;
}

void set_mac(mac* m)
{
    state.mac_ = m;
}
} // namespace usb::df::diag

extern "C" void c2usb_diag_reset(void)
{
    auto* mac_ = state.mac_;
    state = {};
    state.mac_ = mac_;
}

extern "C" void c2usb_diag_dump(void)
{
    auto* mac_ = state.mac_;
    uint32_t now = c2usb_diag_time_ms();

    c2usb_log("c2usb diag (t=%u ms):\n", (unsigned)now);
    if (mac_)
    {
        c2usb_log("  power=%s configured=%d active=%d\n", power_name(mac_->power_state()),
                  (int)mac_->configured(), (int)mac_->active());
    }
    else
    {
        c2usb_log("  no MAC instance\n");
    }
    c2usb_diag_dump_port();
    if (state.ok_count)
    {
        c2usb_log("  last ok transfer: EP %02x, %u ms ago (%u total)\n", state.last_ok_ep,
                  (unsigned)(now - state.last_ok_time), (unsigned)state.ok_count);
    }
    else
    {
        c2usb_log("  no successful data EP transfer yet\n");
    }
    for (uint8_t i = 0; i < diag::CATEGORY_COUNT; i++)
    {
        const auto& s = state.stats[i];
        if (s.total)
        {
            c2usb_log("  %s: total=%u sinceOk=%u, last: EP %02x err=%d %u ms ago\n",
                      category_names[i], (unsigned)s.total, (unsigned)s.since_ok, s.last_ep,
                      s.last_err, (unsigned)(now - s.last_time));
        }
    }
    for (size_t i = 0; i < BUS_RING_SIZE; i++)
    {
        const auto& e = state.bus_ring[(state.bus_ring_next + i) % BUS_RING_SIZE];
        if (e.count)
        {
            c2usb_log("  bus event: %s x%u, last %u ms ago\n", bus_name(e.type), (unsigned)e.count,
                      (unsigned)(now - e.time));
        }
    }
}
