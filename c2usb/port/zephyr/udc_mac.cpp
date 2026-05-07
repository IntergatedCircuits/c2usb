/// @file
///
/// @author Benedek Kupper
/// @date   2024
///
/// @copyright
///         This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
///         If a copy of the MPL was not distributed with this file, You can obtain one at
///         https://mozilla.org/MPL/2.0/.
///
#include "port/zephyr/udc_mac.hpp"
#include <atomic>
#include "compatibility_helper.hpp"
#include <zephyr/logging/log.h>
#include <zephyr/message_queue.hpp>
extern "C"
{
#include <zephyr/drivers/usb/udc.h>
}

#if defined(CONFIG_DEBUG) == defined(NDEBUG)
// for assert() to be active in debug configuration only, this is necessary
#error "Either CONFIG_DEBUG or NDEBUG must be defined"
#endif

using namespace usb::df;

LOG_MODULE_REGISTER(c2usb, CONFIG_C2USB_UDC_MAC_LOG_LEVEL);

static constexpr ::udc_event_type UDC_MAC_TASK = (udc_event_type)-1;

// keeping compatibility with multiple Zephyr versions
#define UDC_CAPS_FLAG(name)                                                                        \
    template <typename T>                                                                          \
    concept Concept_##name = requires(T t) {                                                       \
        { t.name } -> std::convertible_to<bool>;                                                   \
    };                                                                                             \
    template <Concept_##name T>                                                                    \
    constexpr auto name(const T& obj)                                                              \
    {                                                                                              \
        return obj.name;                                                                           \
    }                                                                                              \
    template <typename T>                                                                          \
    constexpr auto name(const T&)                                                                  \
    {                                                                                              \
        return false;                                                                              \
    }
UDC_CAPS_FLAG(addr_before_status)
UDC_CAPS_FLAG(can_detect_vbus)
UDC_CAPS_FLAG(rwup)
UDC_CAPS_FLAG(out_ack)

namespace usb::zephyr
{
constexpr bool udc_init_has_ctx = c2usb::function_arg_count(udc_init) == 3;
template <typename T>
udc_mac* get_event_ctx(T* dev)
    requires(udc_init_has_ctx)
{
    return static_cast<udc_mac*>(const_cast<void*>(udc_get_event_ctx(dev)));
}
[[maybe_unused]] static std::array<std::atomic<udc_mac*>, 2> mac_ptrs;
template <typename T>
udc_mac* get_event_ctx(T* dev)
    requires(!udc_init_has_ctx)
{
    for (auto& mac : mac_ptrs)
    {
        udc_mac* mac_raw = mac.load();
        if (mac_raw and (mac_raw->driver_device() == dev))
        {
            return mac_raw;
        }
    }
    return nullptr;
}

// https://github.com/zephyrproject-rtos/zephyr/pull/103493
template <typename T>
concept udc_data_has_setup_valid = requires(T t) { t.setup_valid; };
constexpr bool udc_managed_ctrl = !udc_data_has_setup_valid<::udc_data>;
extern "C"
{
    int udc_purge_queues(const ::device* dev) __attribute__((weak));
    bool udc_ep_queue_is_empty(const ::device* dev, const uint8_t ep) __attribute__((weak));
}

static int udc_mac_preinit()
{
    static K_THREAD_STACK_DEFINE(stack, CONFIG_C2USB_UDC_MAC_THREAD_STACK_SIZE);
    static k_thread thread_data;

    k_thread_create(&thread_data, stack, K_THREAD_STACK_SIZEOF(stack), &udc_mac::worker, nullptr,
                    nullptr, nullptr, K_PRIO_COOP(8), 0, K_NO_WAIT);
    k_thread_name_set(&thread_data, "c2usb");
    return 0;
}

SYS_INIT(udc_mac_preinit, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

static void set_ctrl_stage(::net_buf* buf, control::stage stage)
{
    auto& info = *udc_get_buf_info(buf);
    info.setup = (stage == control::stage::SETUP);
    info.data = (stage == control::stage::DATA);
    info.status = (stage == control::stage::STATUS);
}

static int udc_buf_enqueue(const ::device* dev, ::net_buf* buf)
{
    auto ret = udc_ep_enqueue(dev, buf);
    if (ret != 0)
    {
        LOG_ERR("Failed to enqueue ep 0x%02x: %d", udc_get_buf_info(buf)->ep, ret);
    }
    return ret;
}

udc_mac::udc_mac(const ::device* dev, size_t ctrl_ep_buf_size)
    : udc_mac(dev, ctrl_ep_buf_size,
              can_detect_vbus(udc_caps(dev)) ? power::state::L3_OFF : power::state::L2_SUSPEND)
{}

udc_mac::udc_mac(const ::device* dev, size_t ctrl_ep_buf_size, usb::power::state power_state)
    : mac(power_state), dev_(dev)
{
    if constexpr (not udc_managed_ctrl)
    {
        size_t ctrl_ep_min_rx = control_ep_max_packet_size(speed::HIGH);

        setup_buf_ = udc_ep_buf_alloc(dev_, endpoint::address::control_out(), ctrl_ep_min_rx);
        assert(setup_buf_ != nullptr);
        set_ctrl_stage(setup_buf_, control::stage::SETUP);

        status_buf_ = udc_ep_buf_alloc(dev_, endpoint::address::control_out(), ctrl_ep_min_rx);
        assert(status_buf_ != nullptr);
        set_ctrl_stage(status_buf_, control::stage::STATUS);

        // TODO: size this according to worst case endpoint usage
        constexpr size_t buf_pool_reserve = 15 * 2 * sizeof(void*);
        ctrl_ep_buf_size =
            CONFIG_UDC_BUF_POOL_SIZE - setup_buf_->size - status_buf_->size - buf_pool_reserve;
    }
    // permanently allocate a buffer for control IN endpoint
    ctrl_buf_ = udc_ep_buf_alloc(dev_, endpoint::address::control_in(), ctrl_ep_buf_size);
    assert(ctrl_buf_ != nullptr);
    if constexpr (not udc_managed_ctrl)
    {
        set_ctrl_stage(ctrl_buf_, control::stage::DATA);
    }
    set_control_buffer(std::span<uint8_t>(ctrl_buf_->data, ctrl_buf_->size));

    set_driver_ctx();
}

void udc_mac::set_driver_ctx()
{
    if constexpr (udc_init_has_ctx)
    {
        return;
    }
    for (auto& mac : mac_ptrs)
    {
        udc_mac* expected = nullptr;
        if (mac.compare_exchange_strong(expected, this))
        {
            return;
        }
    }
    assert(false); // increase mac_ptrs size if this really ever occurs
}

udc_mac::~udc_mac()
{
    if constexpr (not udc_managed_ctrl)
    {
        setup_buf_->ref = 1;
        status_buf_->ref = 1;
        udc_ep_buf_free(dev_, setup_buf_);
        udc_ep_buf_free(dev_, status_buf_);
    }
    ctrl_buf_->ref = 1;
    ctrl_buf_->__buf = control_buffer().data();
    udc_ep_buf_free(dev_, ctrl_buf_);
    if constexpr (udc_init_has_ctx)
    {
        return;
    }
    for (auto& mac : mac_ptrs)
    {
        udc_mac* expected = this;
        if (mac.compare_exchange_strong(expected, nullptr))
        {
            return;
        }
    }
    assert(false);
}

K_MSGQ_DEFINE(udc_mac_msgq, sizeof(udc_event), CONFIG_C2USB_UDC_MAC_MSGQ_SIZE, sizeof(uint32_t));
static auto& message_queue()
{
    return *reinterpret_cast<::zephyr::message_queue<udc_event>*>(&udc_mac_msgq);
}

void udc_mac::worker(void*, void*, void*)
{
    while (true)
    {
        udc_event event = message_queue().get();
        event_callback(event);
    }
}

usb::result udc_mac::post_event(const udc_event& event)
{
    message_queue().post(event);
    return usb::result::ok;
}

static int udc_mac_event_dispatch(const ::device*, const udc_event* event)
{
    static bool last_full = false;
    if (message_queue().full() and last_full)
    {
        // if the queue is full, there's nothing we can do
        return -ENOMEM;
    }
    last_full = !message_queue().try_post(*event);
    if (last_full)
    {
        __ASSERT_PRINT("udc_mac_msgq full\n");
        __ASSERT_POST_ACTION();
    }
#if CONFIG_C2USB_UDC_MAC_LOG_LEVEL >= LOG_LEVEL_DBG
    static auto min_free_msgq_space = message_queue().free_space();
    if (auto free_space = message_queue().free_space(); free_space < min_free_msgq_space)
    {
        min_free_msgq_space = free_space;
        LOG_DBG("udc_mac_msgq free %u", free_space);
    }
#endif
#if 0
    if (k_can_yield())
    {
        // when possible (i.e. sent from coop thread context), try to continue processing
        // the event immediately in the higher level thread
        k_yield();
    }
#endif
    return last_full ? -ENOMSG : 0;
};

void udc_mac::init(const usb::speeds& speeds)
{
    assert(control_ep_max_packet_size(speeds.max) <= ctrl_buf_->size);
    [[maybe_unused]] auto ret = invoke_function(udc_init, dev_, udc_mac_event_dispatch, this);
    assert(ret == 0 and "Failed to initialize UDC");
}

void udc_mac::deinit()
{
    [[maybe_unused]] auto ret = udc_shutdown(dev_);
    assert(ret == 0 and "Failed to shutdown UDC");

    if constexpr (not udc_managed_ctrl)
    {
        ctrl_buf_->ref = 2;
        setup_buf_->ref = 2;
        status_buf_->ref = 2;
        ret = udc_purge_queues(dev_);
        assert(ret == 0 and "Failed to purge endpoint queues");
    }
}

bool udc_mac::set_attached(bool attached)
{
    if (attached == udc_is_enabled(dev_))
    {
        return attached;
    }
    if (power_state() == power::state::L3_OFF)
    {
        return false;
    }
    if (attached)
    {
        if constexpr (not udc_managed_ctrl)
        {
            bool start_setup = udc_ep_queue_is_empty(dev_, USB_CONTROL_EP_OUT);

            [[maybe_unused]] auto ret = udc_enable(dev_);
            assert(ret == 0 and "Failed to enable UDC");

            if (start_setup)
            {
                ctrl_setup();
            }
        }
        else
        {
            [[maybe_unused]] auto ret = udc_enable(dev_);
            assert(ret == 0 and "Failed to enable UDC");
        }
    }
    else
    {
        [[maybe_unused]] auto ret = udc_disable(dev_);
        assert(ret == 0 and "Failed to disable UDC");
    }
    return udc_is_enabled(dev_);
}

usb::result udc_mac::signal_remote_wakeup()
{
#if 0
    if (!rwup(udc_caps(dev_)))
    {
        return usb::result::operation_not_supported;
    }
#endif
    return usb::result(udc_host_wakeup(dev_));
}

usb::speed udc_mac::speed() const
{
    switch (udc_device_speed(dev_))
    {
    case UDC_BUS_SPEED_FS:
        return usb::speed::FULL;
    case UDC_BUS_SPEED_HS:
        return usb::speed::HIGH;
    case UDC_BUS_SPEED_SS:
        // TODO
    default:
        return usb::speed::NONE;
    }
}

void udc_mac::ctrl_stall(net_buf* buf, int err)
{
    auto addr = endpoint::address::control_in();
    if ((request().direction() == usb::direction::OUT) and request().wLength and (err == -ENOMEM))
    {
        addr = endpoint::address::control_out();
    }
    [[maybe_unused]] auto ret = ep_set_stall(addr);

    if constexpr (udc_managed_ctrl)
    {
        if (buf)
        {
            net_buf_unref(buf);
        }
    }
    else
    {
        ctrl_setup();
    }
}

net_buf* udc_mac::move_data_out(net_buf* buf, usb::df::transfer t)
{
    while (buf)
    {
        // consume the buffer chunks into the destination
        auto size = std::min(t.size(), buf->len);
        if (t.data() != buf->data) [[likely]]
        {
            std::memcpy(t.data(), buf->data, t.size());
        }
        t = {t.data() + size, static_cast<decltype(t.size())>(t.size() - size)};

        buf = net_buf_frag_del(nullptr, buf);
        if (buf and udc_get_buf_info(buf)->status)
        {
            break;
        }
    }

    assert(t.empty());
    return buf;
}

void udc_mac::process_ctrl_ep_event(net_buf* buf, const udc_buf_info& info)
{
    endpoint::address addr{info.ep};

    // control bufs are allocated by the UDC driver
    // so they must be freed
    if (info.setup)
    {
        // the UDC driver might send a new setup packet without having
        // processed or cleaned up the last pending response
        // so we try to do that here instead
        // when there's no effect, "ep 0x80 is not halted|disabled" will be logged
        udc_ep_dequeue(dev_, USB_CONTROL_EP_IN);

        assert((info.ep == USB_CONTROL_EP_OUT) and (buf->len == sizeof(request())));

        // new setup packet resets the stall status
        stall_flags_.clear(endpoint::address::control_out());
        stall_flags_.clear(endpoint::address::control_in());

        std::memcpy(&request(), buf->data, sizeof(request()));
#if CONFIG_C2USB_UDC_MAC_LOG_LEVEL >= LOG_LEVEL_DBG
        // logged when DBG level is selected, but without the extra details of LOG_DBG()
        LOG_INF("CTRL EP setup: %02x %02x %04x %u", request().bmRequestType, request().bRequest,
                (uint16_t)request().wValue, (uint16_t)request().wLength);
#endif
        // pop the buf chain for the next stage(s), freeing the setup buf
        buf = net_buf_frag_del(nullptr, buf);

        if (request().direction() == usb::direction::IN)
        {
            // descriptors are fully assembled in control_buffer even if the host only requests
            // the "header", so instead of the UDC allocated buf, use the ctrl_buf_'s buffer
            if (buf)
            {
                net_buf_unref(buf);
            }

            buf = udc_ep_buf_alloc(dev_, endpoint::address::control_in(), 0);
            if (!buf)
            {
                LOG_ERR("Control EP data cannot be allocated, stalling EP");
                return ctrl_stall(buf, -ENOMEM);
            }
            buf->flags |= NET_BUF_EXTERNAL_DATA;
            auto& new_info = *udc_get_buf_info(buf);
            new_info.data = true;
        }
        else if (buf == nullptr)
        {
            // control OUT transfers are received in advance,
            // cannot provide new buffer afterwards
            LOG_ERR("Control OUT data couldn't be allocated, stalling EP");
            return ctrl_stall(buf, -ENOMEM);
        }

        // step 1. setup stage
        auto transfer = control_ep_setup();
        if (transfer.stalled())
        {
            return ctrl_stall(buf);
        }

        // step 2. data stage
        if (transfer.size() > 0)
        {
            if (request().direction() == direction::OUT)
            {
                // the data has been received in advance
                buf = move_data_out(buf, transfer);
                if (!control_ep_data(direction::OUT, transfer))
                {
                    return ctrl_stall(buf);
                }
            }
            else // direction::IN
            {
                buf->data = transfer.data();
                buf->__buf = buf->data;
                buf->len = transfer.size();
                if (control_in_zlp(transfer))
                {
                    udc_ep_buf_set_zlp(buf);
                }
            }
        }
        else // no data -> status stage
        {
            buf->len = 0;
            buf->size = 0;

            // setting the address early
            if (addr_before_status(udc_caps(dev_)) and (request() == standard::device::SET_ADDRESS))
                [[unlikely]]
            {
                [[maybe_unused]] auto ret = udc_set_address(dev_, request().wValue.low_byte());
                assert(ret == 0);
            }
        }

        udc_buf_enqueue(dev_, buf);
    }
    else if (info.status and !info.err)
    {
        // status IN complete
        net_buf_unref(buf);

        // timely address setting
        if (!addr_before_status(udc_caps(dev_)) and (request() == standard::device::SET_ADDRESS))
            [[unlikely]]
        {
            [[maybe_unused]] auto ret = udc_set_address(dev_, request().wValue.low_byte());
            assert(ret == 0);
        }
    }
    else
    {
        net_buf_unref(buf);
        LOG_WRN("CTRL EP %x (stage %d) error: %d", info.ep,
                info.setup * 0 + info.data * 1 + info.status * 2, info.err);
    }
}

usb::result udc_mac::queue_task(etl::delegate<void()> task)
{
    udc_event event{.type = UDC_MAC_TASK};
    static_assert(offsetof(udc_event, type) == 0 and
                  sizeof(task) <= (sizeof(event) - offsetof(udc_event, value)));
    std::memcpy(&event.value, &task, sizeof(task));
    return post_event(event);
}

int udc_mac::event_callback(const udc_event& event)
{
    if (event.type == UDC_MAC_TASK) [[unlikely]]
    {
        auto& task = reinterpret_cast<const etl::delegate<void()>&>(event.value);
        task();
        return 0;
    }
    auto* mac = get_event_ctx(event.dev);
    assert(mac != nullptr);
    return mac->process_event(event);
}

int udc_mac::process_event(const udc_event& event)
{
    if ((power_state() == power::state::L3_OFF) and (event.type != UDC_EVT_VBUS_READY)) [[unlikely]]
    {
        // flush late events after Vbus removal
        return 0;
    }
    switch (event.type)
    {
    case UDC_EVT_EP_REQUEST:
        process_ep_event(event.buf);
        break;
    case UDC_EVT_RESET:
        bus_reset();
        {
            [[maybe_unused]] auto ret = udc_set_address(dev_, 0);
            assert(ret == 0);
            // control endpoints are opened in udc_init

            if constexpr (udc_managed_ctrl)
            {
                // clear leftover pending ctrl data
                ret = udc_ep_dequeue(dev_, USB_CONTROL_EP_IN);
            }
        }
        break;
    case UDC_EVT_SUSPEND:
        set_power_state(power::state::L2_SUSPEND);
        break;
    case UDC_EVT_RESUME:
        set_power_state(power::state::L0_ON);
        break;
    case UDC_EVT_VBUS_READY:
        set_power_state(power::state::L2_SUSPEND);
        set_attached(active());
        break;
    case UDC_EVT_VBUS_REMOVED:
        assert(power_state() != power::state::L3_OFF);
        set_attached(false);
        set_power_state(power::state::L3_OFF);
        break;
    case UDC_EVT_SOF:
    case UDC_EVT_ERROR:
    default:
        break;
    }
    return 0;
}

void udc_mac::ctrl_setup()
{
    net_buf_reset(setup_buf_);
    udc_buf_enqueue(dev_, setup_buf_);
}

void udc_mac::ctrl_status(usb::direction dir)
{
    auto& info = *udc_get_buf_info(status_buf_);
    info.ep = endpoint::address::control(dir);
    net_buf_reset(status_buf_); // TODO: is this necessary?

    if (dir == direction::OUT and out_ack(udc_caps(dev_))) [[unlikely]]
    {
        // auto acknowledge status OUT
    }
    else if (udc_buf_enqueue(dev_, status_buf_) != 0)
    {
        return ctrl_stall();
    }
    ctrl_setup();
}

void udc_mac::process_ctrl_ep(net_buf* buf, const udc_buf_info& info)
{
    direction dir = endpoint::address(info.ep).direction();

    if (info.err)
    {
        LOG_WRN("CTRL EP %x (stage %d) error: %d", info.ep,
                info.setup * 0 + info.data * 1 + info.status * 2, info.err);

        if (info.setup or (info.data and (dir == direction::OUT)))
        {
            ctrl_setup();
        }
        return;
    }

    if (info.setup)
    {
        assert((info.ep == USB_CONTROL_EP_OUT) and (buf->len == sizeof(request())));

        // new setup packet resets the stall status
        stall_flags_.clear(endpoint::address::control_out());
        stall_flags_.clear(endpoint::address::control_in());

        std::memcpy(&request(), buf->data, sizeof(request()));
#if CONFIG_C2USB_UDC_MAC_LOG_LEVEL >= LOG_LEVEL_DBG
        // logged when DBG level is selected, but without the extra details of LOG_DBG()
        LOG_INF("CTRL EP setup: %02x %02x %04x %u", request().bmRequestType, request().bRequest,
                (uint16_t)request().wValue, (uint16_t)request().wLength);
#endif

        auto transfer = control_ep_setup();
        if (transfer.stalled())
        {
            return ctrl_stall();
        }

        // initiate DATA stage
        if (transfer.size() > 0)
        {
            auto& data_info = *udc_get_buf_info(ctrl_buf_);
            ctrl_buf_->data = transfer.data();
            ctrl_buf_->__buf = ctrl_buf_->data;

            if (request().direction() == direction::OUT)
            {
                ctrl_buf_->size = ROUND_UP(transfer.size(), control_ep_max_packet_size(speed()));
                ctrl_buf_->len = 0;
                data_info.ep = endpoint::address::control_out();
                data_info.zlp = false;

                udc_buf_enqueue(dev_, ctrl_buf_);
            }
            else // direction::IN
            {
                ctrl_buf_->size = transfer.size();
                ctrl_buf_->len = transfer.size();
                data_info.ep = endpoint::address::control_in();
                data_info.zlp = control_in_zlp(transfer);

                udc_buf_enqueue(dev_, ctrl_buf_) == 0
                    ? ctrl_status(direction::OUT) // status OUT must be provided early
                    : ctrl_stall();
            }
        }
        else // no DATA -> initiate STATUS IN stage
        {
            // setting the address early
            if (addr_before_status(udc_caps(dev_)) and (request() == standard::device::SET_ADDRESS))
                [[unlikely]]
            {
                [[maybe_unused]] auto ret = udc_set_address(dev_, request().wValue.low_byte());
                assert(ret == 0 and "Failed to set address");
            }

            ctrl_status(direction::IN);
        }
    }
    else if (info.data)
    {
        bool ok = control_ep_data(dir, {buf->data, buf->len});

        if (request().direction() == direction::OUT)
        {
            return ok ? ctrl_status(direction::IN) // initiate STATUS IN stage / stall
                      : ctrl_stall();
        }
        else
        {
            // status OUT was provided early
            assert(ok and "DATA IN stage rejected");
        }
    }
    else if (info.status)
    {
        // timely address setting
        if (not addr_before_status(udc_caps(dev_)) and (request() == standard::device::SET_ADDRESS))
            [[unlikely]]
        {
            [[maybe_unused]] auto ret = udc_set_address(dev_, request().wValue.low_byte());
            assert(ret == 0 and "Failed to set address");
        }
    }
    else
    {
        assert(false and "Invalid control buf received");
    }
}

void udc_mac::process_ep_event(net_buf* buf)
{
    auto& info = *udc_get_buf_info(buf);
    endpoint::address addr{info.ep};
    if (addr.number() == 0)
    {
        if constexpr (udc_managed_ctrl)
        {
            process_ctrl_ep_event(buf, info);
        }
        else
        {
            process_ctrl_ep(buf, info);
        }
    }
    else
    {
        busy_flags_.clear(addr);

        if (info.err == 0)
        {
            for (uint8_t i = 0; i < ep_bufs_.size(); ++i)
            {
                if (ep_bufs_[i] == buf)
                {
                    return ep_transfer_complete(addr, create_ep_handle(i + 1),
                                                transfer(buf->data, buf->len));
                }
            }
            assert(ep_bufs_.size() == 0); // a net_buf was issued out of c2usb scope
        }
        else
        {
            LOG_ERR("EP %x error:%d", info.ep, info.err);
        }
    }
}

void udc_mac::allocate_endpoints(config::view config)
{
    // first clean up the previous allocation
    for (auto it = ep_bufs_.rbegin(); it != ep_bufs_.rend(); ++it)
    {
        auto* buf = *it;
        if (buf == ep_bufs_.front())
        {
            // restore allocated pointer so it can be freed
            buf->flags &= ~NET_BUF_EXTERNAL_DATA;
            buf->__buf = reinterpret_cast<uint8_t*>(ep_bufs_.data());
        }
        [[maybe_unused]] auto ret = udc_ep_buf_free(dev_, buf);
        assert(ret == 0);
    }
    ep_bufs_ = {};

    // allocate new buffers
    auto ep_bufs_count = config.active_endpoints().count();
    if (ep_bufs_count == 0)
    {
        return;
    }
    [[maybe_unused]] constexpr size_t ctrl_ep_buf_demand = 4; // 2 * (setup + data)
    assert(CONFIG_UDC_BUF_COUNT >= (ctrl_ep_buf_demand + ep_bufs_count));

    // abuse the buffer pool to allocate the pointer array storage
    // on the first buffer
    size_t alloc_size = ep_bufs_count * sizeof(void*);
    uint8_t i = 0;
    for (auto& ep : config.active_endpoints())
    {
        auto* buf = udc_ep_buf_alloc(dev_, ep.address(), alloc_size);
        assert(buf != nullptr);
        buf->flags |= NET_BUF_EXTERNAL_DATA;

        if (alloc_size != 0)
        {
            assert(alignof(buf->__buf) == alignof(void*));
            ep_bufs_ = {reinterpret_cast<::net_buf**>(buf->__buf), ep_bufs_count};
            alloc_size = 0;
        }

        // save each buf pointer
        ep_bufs_[i] = buf;
        i++;
    }
}

usb::df::ep_handle udc_mac::ep_address_to_handle(endpoint::address addr) const
{
    for (uint8_t i = 0; i < ep_bufs_.size(); ++i)
    {
        if (udc_get_buf_info(ep_bufs_[i])->ep == addr)
        {
            return create_ep_handle(i + 1);
        }
    }
    return {};
}

usb::df::ep_handle udc_mac::ep_config_to_handle(const config::endpoint& ep) const
{
    return ep_address_to_handle(ep.address());
}

::net_buf* const& udc_mac::ep_handle_to_buf(ep_handle eph) const
{
    assert((eph != ep_handle()) and (size_t(eph) <= ep_bufs_.size()));
    return ep_bufs_[eph - 1];
}

usb::endpoint::address udc_mac::ep_handle_to_address(ep_handle eph) const
{
    return endpoint::address(udc_get_buf_info(ep_handle_to_buf(eph))->ep);
}

usb::df::ep_handle udc_mac::ep_open(const usb::df::config::endpoint& ep)
{
    auto ret = udc_ep_enable(dev_, ep.address(), std::bit_cast<uint8_t>(ep.bmAttributes),
                             ep.wMaxPacketSize, ep.bInterval);
    if (ret != 0)
    {
        return {};
    }

    auto eph = ep_address_to_handle(ep.address());
    auto& info = *udc_get_buf_info(ep_handle_to_buf(eph));
    assert(info.owner == nullptr);
    info.owner = (void*)(&ep);
    busy_flags_.clear(ep.address());
    return eph;
}

usb::result udc_mac::ep_transfer(usb::df::ep_handle eph, const transfer& t, usb::direction dir)
{
    auto* buf = ep_handle_to_buf(eph);
    auto& info = *udc_get_buf_info(buf);
    assert((buf != nullptr) and (info.owner != nullptr));
    auto addr = endpoint::address(info.ep);
#if 0 // using private API
    auto* cfg = udc_get_ep_cfg(dev_, addr);
    if (!k_fifo_is_empty(cfg->fifo))
    {
        return result::device_or_resource_busy;
    }
#endif
    if ((dir == direction::IN) and (power_state() != power::state::L0_ON))
    {
        return result::network_down;
    }
    if (busy_flags_.test_and_set(addr))
    {
        return result::device_or_resource_busy;
    }

    buf->data = t.data();
    buf->__buf = buf->data;
    buf->size = t.size();
    buf->len = dir == direction::OUT ? 0 : t.size();
    auto ret = udc_ep_enqueue(dev_, buf);
    if (ret != 0)
    {
        busy_flags_.clear(addr);
    }
    return usb::result(ret);
}

usb::result udc_mac::ep_send(usb::df::ep_handle eph, const std::span<const uint8_t>& data)
{
    return ep_transfer(eph, data, direction::IN);
}

usb::result udc_mac::ep_receive(usb::df::ep_handle eph, const std::span<uint8_t>& data)
{
    return ep_transfer(eph, data, direction::OUT);
}

usb::result udc_mac::ep_close(usb::df::ep_handle eph)
{
    auto& info = *udc_get_buf_info(ep_handle_to_buf(eph));
    auto addr = endpoint::address(info.ep);
    assert(info.owner != nullptr);
    info.owner = nullptr;
    auto ret = udc_ep_disable(dev_, addr);
    if (ret != 0)
    {
        return usb::result(ret);
    }
    ret = udc_ep_dequeue(dev_, addr);
    if (ret != 0)
    {
        return usb::result(ret);
    }
    k_yield();
    return usb::result::OK;
}

usb::result udc_mac::ep_cancel(usb::df::ep_handle eph)
{
    auto addr = ep_handle_to_address(eph);
    auto ret = udc_ep_dequeue(dev_, addr);
    if (ret == 0)
    {
        busy_flags_.clear(addr);
    }
    return usb::result(ret);
}

usb::result udc_mac::ep_set_stall(endpoint::address addr)
{
    auto ret = udc_ep_set_halt(dev_, addr);
    if (ret == 0)
    {
        // TODO: set busy flag?
        stall_flags_.test_and_set(addr);
    }
    return usb::result(ret);
}

usb::result udc_mac::ep_clear_stall(endpoint::address addr)
{
    auto ret = udc_ep_clear_halt(dev_, addr);
    if (ret == 0)
    {
        stall_flags_.clear(addr);
        // TODO: clear busy flag?
    }
    return usb::result(ret);
}

bool udc_mac::ep_is_stalled(usb::df::ep_handle eph) const
{
    return stall_flags_.test(ep_handle_to_address(eph));
}

usb::result udc_mac::ep_change_stall(usb::df::ep_handle eph, bool stall)
{
    auto addr = ep_handle_to_address(eph);
    if (stall)
    {
        return ep_set_stall(addr);
    }
    else
    {
        return ep_clear_stall(addr);
    }
}

} // namespace usb::zephyr
