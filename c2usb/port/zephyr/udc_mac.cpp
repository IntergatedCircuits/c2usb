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

#if C2USB_HAS_ZEPHYR_HEADERS
#include <atomic>
#include "port/compatibility_helper.hpp"
#include <zephyr/logging/log.h>
extern "C"
{
#include <zephyr/drivers/usb/udc.h>
}

using namespace usb::zephyr;
using namespace usb::df;

LOG_MODULE_REGISTER(c2usb, CONFIG_C2USB_UDC_MAC_LOG_LEVEL);

static constexpr ::udc_event_type UDC_MAC_TASK = (udc_event_type)-1;

// keeping compatibility with multiple Zephyr versions
template <typename T>
concept HasAddrBeforeStatus = requires(T t) {
    { t.addr_before_status } -> std::convertible_to<bool>;
};
template <HasAddrBeforeStatus T>
constexpr auto addr_before_status(const T& obj)
{
    return obj.addr_before_status;
}
template <typename T>
constexpr auto addr_before_status(const T&)
{
    return false;
}

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
        if (mac_raw and (mac_raw->device() == dev))
        {
            return mac_raw;
        }
    }
    return nullptr;
}

K_MSGQ_DEFINE(udc_mac_msgq, sizeof(udc_event), CONFIG_C2USB_UDC_MAC_MSGQ_SIZE, sizeof(uint32_t));

static int udc_mac_preinit()
{
    static K_THREAD_STACK_DEFINE(stack, CONFIG_C2USB_UDC_MAC_THREAD_STACK_SIZE);
    static k_thread thread_data;
    auto worker = [](void*, void*, void*)
    {
        while (true)
        {
            udc_event event;
            k_msgq_get(&udc_mac_msgq, &event, K_FOREVER);
            udc_mac::event_callback(event);
        }
    };

    k_thread_create(&thread_data, stack, K_THREAD_STACK_SIZEOF(stack), worker, nullptr, nullptr,
                    nullptr, K_PRIO_COOP(8), 0, K_NO_WAIT);
    k_thread_name_set(&thread_data, "c2usb");
    return 0;
}

SYS_INIT(udc_mac_preinit, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

udc_mac::udc_mac(const ::device* dev)
    : mac(), dev_(dev)
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

usb::result udc_mac::post_event(const udc_event& event)
{
    return static_cast<usb::result>(k_msgq_put(&udc_mac_msgq, &event, K_NO_WAIT));
}

void udc_mac::init(const usb::speeds& speeds)
{
    auto dispatch = [](const ::device*, const udc_event* event)
    {
        return k_msgq_put(&udc_mac_msgq, event, K_NO_WAIT);
    };
    [[maybe_unused]] auto ret = invoke_function(udc_init, dev_, dispatch, this);
    assert(ret == 0);
}

void udc_mac::deinit()
{
    [[maybe_unused]] auto ret = udc_shutdown(dev_);
    assert(ret == 0);
}

bool udc_mac::set_attached(bool attached)
{
    if (attached)
    {
        [[maybe_unused]] auto ret = udc_enable(dev_);
        assert(ret == 0);
    }
    else
    {
        [[maybe_unused]] auto ret = udc_disable(dev_);
        assert(ret == 0);
    }
    return udc_is_enabled(dev_);
}

void udc_mac::signal_remote_wakeup()
{
    [[maybe_unused]] auto ret = udc_host_wakeup(dev_);
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

void udc_mac::control_ep_open()
{
    [[maybe_unused]] auto ret = udc_set_address(dev_, 0);
    assert(ret == 0);
    // control endpoints are opened in udc_init

    // clear leftover pending ctrl data
    ret = udc_ep_dequeue(dev_, USB_CONTROL_EP_IN);
    assert(ret == 0);
}

void udc_mac::move_data_out(usb::df::transfer t)
{
    while (ctrl_buf_)
    {
        // consume the buffer chunks into the destination
        auto size = std::min(t.size(), ctrl_buf_->len);
        if (t.data() != ctrl_buf_->data) [[likely]]
        {
            std::memcpy(t.data(), ctrl_buf_->data, t.size());
        }
        t = {t.data() + size, static_cast<decltype(t.size())>(t.size() - size)};

        ctrl_buf_ = net_buf_frag_del(nullptr, ctrl_buf_);
        if (ctrl_buf_ and udc_get_buf_info(ctrl_buf_)->status)
        {
            break;
        }
    }

    assert(t.empty());
}

void udc_mac::control_reply(usb::direction dir, const usb::df::transfer& t)
{
    auto addr = endpoint::address::control(dir);

    if (t.stalled()) [[unlikely]]
    {
        if (ctrl_buf_)
        {
            net_buf_unref(ctrl_buf_);
            ctrl_buf_ = nullptr;
        }
        [[maybe_unused]] auto ret = ep_set_stall(addr);
        assert(ret == result::OK);
        return;
    }

    if (t.size() > 0)
    {
        assert((ctrl_buf_ != nullptr) and udc_get_buf_info(ctrl_buf_)->data);
        if (dir == direction::IN)
        {
            ctrl_buf_->data = t.data();
            ctrl_buf_->len = t.size();

            // set ZLP when needed
            if ((request().wLength > t.size()) and
                ((t.size() % control_ep_max_packet_size(speed())) == 0))
            {
                udc_ep_buf_set_zlp(ctrl_buf_);
            }
        }
        else
        {
            // the data has been received in advance
            move_data_out(t);

            // complete the data stage for upper layer, recursive call ahead
            // the data can be rejected and end with a stall
            control_ep_data(dir, t);
        }
    }
    else if (ctrl_buf_ != nullptr)
    {
        ctrl_buf_->len = 0;
        ctrl_buf_->size = 0;

        // setting the address early
        if (addr_before_status(udc_caps(dev_)) and (request() == standard::device::SET_ADDRESS))
        {
            auto ret = udc_set_address(dev_, request().wValue.low_byte());
            assert(ret == 0);
        }
    }

    // only one buf chain is sent to driver (data + status)
    if (ctrl_buf_ != nullptr)
    {
        auto ret = udc_ep_enqueue(dev_, ctrl_buf_);
        assert(ret == 0);
        ctrl_buf_ = nullptr;
    }

    if ((t.size() > 0) and (dir == direction::IN))
    {
        // TODO: this should be done between the data IN and status OUT stages,
        // but the UDC driver is designed to perform these two without providing an event
        // so for now we do it on best effort basis (it should only be important for DFU class)
        control_ep_data(dir, t);
    }
}

void udc_mac::process_ctrl_ep_event(net_buf* buf, const udc_buf_info& info)
{
    endpoint::address addr{info.ep};
    int err = info.err;

    // control bufs are allocated by the UDC driver
    // so they must be freed
    if (info.setup)
    {
        assert((info.ep == USB_CONTROL_EP_OUT) and (buf->len == sizeof(request())));

        // new setup packet resets the stall status
        stall_flags_.clear(endpoint::address::control_out());
        stall_flags_.clear(endpoint::address::control_in());

        std::memcpy(&request(), buf->data, sizeof(request()));

        // pop the buf chain for the next stage(s), freeing the setup buf
        ctrl_buf_ = net_buf_frag_del(nullptr, buf);
        if (ctrl_buf_ == nullptr)
        {
            control_stall();
            return;
        }
        if (request().direction() == direction::IN)
        {
            assert(udc_get_buf_info(ctrl_buf_)->data);
            // reserve all space for ctrl data
            // as it's used to construct descriptors even if the host request truncates it
            auto* status = net_buf_frag_del(nullptr, ctrl_buf_);

            // magic number, tune it with below code fragment
            static const size_t max_alloc_size = CONFIG_UDC_BUF_POOL_SIZE - 96;
            static_assert(CONFIG_UDC_BUF_POOL_SIZE > 128);
            ctrl_buf_ = udc_ep_buf_alloc(dev_, endpoint::address::control_in(),
                                         max_alloc_size - ep_bufs_.size_bytes());
#if 0
                alloc_size_tuned = max_alloc_size;
                while (ctrl_buf_ == nullptr)
                {
                    alloc_size_tuned -= sizeof(std::intptr_t);
                    assert(alloc_size_tuned > ep_bufs_.size_bytes());
                    ctrl_buf_ = udc_ep_buf_alloc(dev_, endpoint::address::control_in(),
                            alloc_size_tuned - ep_bufs_.size_bytes());
                }
#endif
            assert(ctrl_buf_ != nullptr);
            udc_get_buf_info(ctrl_buf_)->data = true;
            if (status != nullptr)
            {
                net_buf_frag_add(ctrl_buf_, status);
            }
        }

        // set up the buf as ctrl data target
        set_control_buffer(std::span<uint8_t>(ctrl_buf_->data, ctrl_buf_->size));

        // signal upper layer
        control_ep_setup();
    }
    else if (info.status and (err == 0))
    {
        // we only get callback here if there was no data stage
        // therefore control_reply has to call control_ep_data in all other cases
        assert(ctrl_buf_ == nullptr);

        // signal upper layer
        control_ep_data(addr.direction(), transfer(buf->data, buf->len));

        net_buf_unref(buf);

        // timely address setting
        if (!addr_before_status(udc_caps(dev_)) and (request() == standard::device::SET_ADDRESS))
        {
            auto ret = udc_set_address(dev_, request().wValue.low_byte());
            assert(ret == 0);
        }
    }
    else
    {
        net_buf_unref(buf);
        LOG_ERR("CTRL EP %x error:%d", *reinterpret_cast<const uint16_t*>(&info), err);
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
        auto& task =
            *reinterpret_cast<etl::delegate<void()>*>(&const_cast<udc_event&>(event).value);
        task();
        return 0;
    }
    auto* mac = get_event_ctx(event.dev);
    assert(mac != nullptr);
    return mac->process_event(event);
}

int udc_mac::process_event(const udc_event& event)
{
    switch (event.type)
    {
    case UDC_EVT_EP_REQUEST:
        process_ep_event(event.buf);
        break;
    case UDC_EVT_RESET:
        bus_reset();
        break;
    case UDC_EVT_SUSPEND:
        set_power_state(power::state::L2_SUSPEND);
        break;
    case UDC_EVT_RESUME:
    case UDC_EVT_VBUS_READY:
        set_power_state(power::state::L0_ON);
        break;
    case UDC_EVT_VBUS_REMOVED:
        // FIXME dodging race condition between this and RESUME:
        // re-queue the event to be processed after
        if (event.status == 0)
        {
            udc_event e2 = event;
            e2.status++;
            post_event(e2);
            break;
        }
        set_power_state(power::state::L3_OFF);
        break;
    case UDC_EVT_SOF:
    case UDC_EVT_ERROR:
    default:
        break;
    }
    return 0;
}

void udc_mac::process_ep_event(net_buf* buf)
{
    auto& info = *udc_get_buf_info(buf);
    endpoint::address addr{info.ep};
    int err = info.err;
    if (addr.number() == 0)
    {
        process_ctrl_ep_event(buf, info);
    }
    else
    {
        busy_flags_.clear(addr);

        if (err == 0)
        {
            for (uint8_t i = 0; i < ep_bufs_.size(); ++i)
            {
                if (ep_bufs_[i] == buf)
                {
                    ep_transfer_complete(create_ep_handle(i + 1), transfer(buf->data, buf->len));
                    return;
                }
            }
            assert(ep_bufs_.size() == 0); // a net_buf was issued out of c2usb scope
        }
        else
        {
            LOG_ERR("EP %x error:%d", *reinterpret_cast<uint16_t*>(&info), err);
        }
    }
}

void udc_mac::allocate_endpoints(config::view config)
{
    // first clean up the previous allocation
    for (auto* buf : ep_bufs_)
    {
        auto ret = udc_ep_buf_free(dev_, buf);
        assert(ret == 0);
    }
    ep_bufs_ = {};

    // allocate new buffers
    auto ep_bufs_count = config.active_endpoints().count();
    if (ep_bufs_count > 0)
    {
        constexpr size_t ctrl_ep_buf_demand = 3; // setup + data + status
        assert(CONFIG_UDC_BUF_COUNT >= (ctrl_ep_buf_demand + ep_bufs_count));

        // abuse the buffer pool to allocate the pointer array storage
        // on the first buffer
        size_t alloc_size = ep_bufs_count * sizeof(void*);
        uint8_t i = 0;
        for (auto& ep : config.active_endpoints())
        {
            auto* buf = udc_ep_buf_alloc(dev_, ep.address(), alloc_size);
            assert(buf != nullptr);

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

const usb::df::config::endpoint& udc_mac::ep_handle_to_config(ep_handle eph) const
{
    auto* ptr = udc_get_buf_info(ep_handle_to_buf(eph))->owner;
    if (ptr != nullptr)
    {
        return *(const usb::df::config::endpoint*)(ptr);
    }
    return reinterpret_cast<const usb::df::config::endpoint&>(config::footer());
}

usb::endpoint::address udc_mac::ep_handle_to_address(ep_handle eph) const
{
    return endpoint::address(udc_get_buf_info(ep_handle_to_buf(eph))->ep);
}

usb::df::ep_handle udc_mac::ep_open(const usb::df::config::endpoint& ep)
{
    auto ret = udc_ep_enable(dev_, ep.address(), ep.bmAttributes, ep.wMaxPacketSize, ep.bInterval);
    if (ret != 0)
    {
        return {};
    }

    auto eph = ep_address_to_handle(ep.address());
    auto& info = *udc_get_buf_info(ep_handle_to_buf(eph));
    assert(info.owner == nullptr);
    info.owner = (void*)(&ep);
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
        return result::BUSY;
    }
#endif
    if (busy_flags_.test_and_set(addr))
    {
        return result::BUSY;
    }

    buf->data = t.data();
    buf->size = t.size();
    buf->len = dir == direction::OUT ? 0 : t.size();
    auto ret = udc_ep_enqueue(dev_, buf);
    if (ret != 0)
    {
        busy_flags_.clear(addr);
    }
    return static_cast<usb::result>(ret);
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
    busy_flags_.clear(addr);
    return static_cast<usb::result>(ret);
}

usb::result udc_mac::ep_cancel(usb::df::ep_handle eph)
{
    auto addr = ep_handle_to_address(eph);
    auto ret = udc_ep_dequeue(dev_, addr);
    if (ret == 0)
    {
        busy_flags_.clear(addr);
    }
    return static_cast<usb::result>(ret);
}

usb::result udc_mac::ep_set_stall(endpoint::address addr)
{
    auto ret = udc_ep_set_halt(dev_, addr);
    if (ret == 0)
    {
        // TODO: set busy flag?
        stall_flags_.test_and_set(addr);
    }
    return static_cast<usb::result>(ret);
}

usb::result udc_mac::ep_clear_stall(endpoint::address addr)
{
    auto ret = udc_ep_clear_halt(dev_, addr);
    if (ret == 0)
    {
        stall_flags_.clear(addr);
        // TODO: clear busy flag?
    }
    return static_cast<usb::result>(ret);
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

#endif // C2USB_HAS_ZEPHYR_HEADERS
