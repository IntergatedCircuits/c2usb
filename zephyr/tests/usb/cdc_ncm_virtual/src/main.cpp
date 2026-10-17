#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/socket.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/ztest.h>

extern "C"
{
#include <zephyr/drivers/usb/udc.h>
}

#include <algorithm>
#include <array>
#include <optional>

#include <usb/class/cdc_ncm_ntb.hpp>
#include <usb/df/class/cdc_ncm.hpp>
#include <usb/df/config_factory.hpp>
#include <usb/df/device.hpp>
#include <usb/df/vendor/zephyr/cdc_ncm_ethernet.hpp>
#include <usb/df/vendor/zephyr/udc_mac.hpp>
#include <usb/product_info.hpp>
#include <usb/standard/descriptor_set.hpp>
#include <usb/standard/descriptors.hpp>
#include <usb_host.hpp>
#include <zephyr/thread.hpp>

#include "ncm_host_peer.hpp"

LOG_MODULE_REGISTER(c2usb_usb_cdc_ncm_virtual, LOG_LEVEL_DBG);

using namespace zephyr;
using namespace c2usb;
using namespace std::chrono_literals;

namespace
{
constexpr usb::cdc::mac_address HOST_MAC{0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
constexpr usb::cdc::mac_address DEVICE_MAC{0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
constexpr usb::product_info PRODUCT_INFO{0x2fe3, "C2USB", 0x1202, "C2USB NCM Loop Test",
                                         usb::version("1.0")};
constexpr uint8_t DATA_INTERFACE = 1;
constexpr usb::endpoint::address BULK_OUT{0x02};
constexpr usb::endpoint::address BULK_IN{0x82};
constexpr usb::endpoint::address NOTIFY_IN{0x81};
constexpr std::array<uint8_t, 4> LOCAL_IP{192, 0, 2, 10};
constexpr std::array<uint8_t, 4> SECONDARY_LOCAL_IP{192, 0, 2, 11};
constexpr std::array<uint8_t, 4> HOST_IP{192, 0, 2, 2};
constexpr uint16_t TEST_UDP_PORT = 4242;
constexpr size_t RX_DATAGRAM_COUNT = 4;
constexpr size_t TX_DATAGRAM_COUNT = 48;
constexpr size_t UDP_PAYLOAD_SIZE = 128;

K_THREAD_STACK_DEFINE(udp_sender_stack, 3072);
struct k_thread udp_sender_thread;
bool udp_sender_started{};
K_THREAD_STACK_DEFINE(udp_receiver_stack, 3072);
struct k_thread udp_receiver_thread;
bool udp_receiver_started{};
int udp_socket_fd = -1;
K_THREAD_STACK_DEFINE(blocked_tx_stack, 3072);
struct k_thread blocked_tx_thread;
bool blocked_tx_started{};

template <size_t SIZE>
using ntb_array = std::array<uint32_t, SIZE>;

C2USB_USB_TRANSFER_ALIGN(static ntb_array<1024>, device_ntb_in){};
C2USB_USB_TRANSFER_ALIGN(static ntb_array<1024>, device_ntb_out){};

usb::df::zephyr::udc_mac& usb_mac()
{
    static usb::df::zephyr::udc_mac mac{DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 512};
    return mac;
}

std::optional<usb::df::device_instance<usb::speeds(usb::speed::FULL, usb::speed::HIGH)>>& device()
{
    static std::optional<usb::df::device_instance<usb::speeds(usb::speed::FULL, usb::speed::HIGH)>>
        instance;
    return instance;
}

usb::df::cdc::ncm::function* ncm_function{};
::net_if* ncm_netif{};
usb::test::device* host_device{};

void set_alt_setting(uint8_t interface, uint8_t alternate);

uint16_t ipv4_checksum(std::span<const uint8_t> header)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < header.size(); i += 2)
    {
        sum += *std_layout_cast<const be_uint16_t*>(header.data() + i);
    }
    while ((sum >> 16) != 0)
    {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return uint16_t(~sum);
}

void write_mac(uint8_t* dst, const usb::cdc::mac_address& mac)
{
    std::copy(mac.begin(), mac.end(), dst);
}

void write_ipv4(uint8_t* dst, const std::array<uint8_t, 4>& address)
{
    std::copy(address.begin(), address.end(), dst);
}

template <size_t PAYLOAD_SIZE>
auto make_udp_frame(const std::array<uint8_t, PAYLOAD_SIZE>& payload,
                    const std::array<uint8_t, 4>& source_ip,
                    const std::array<uint8_t, 4>& destination_ip, uint16_t source_port,
                    uint16_t destination_port)
{
    std::array<uint8_t, 14 + 20 + 8 + PAYLOAD_SIZE> frame{};
    write_mac(frame.data(), DEVICE_MAC);
    write_mac(frame.data() + 6, HOST_MAC);
    *std_layout_cast<be_uint16_t*>(frame.data() + 12) = 0x0800;

    auto* ip = frame.data() + 14;
    ip[0] = 0x45;
    *std_layout_cast<be_uint16_t*>(ip + 2) = uint16_t(20 + 8 + PAYLOAD_SIZE);
    *std_layout_cast<be_uint16_t*>(ip + 4) = 1;
    *std_layout_cast<be_uint16_t*>(ip + 6) = 0x4000;
    ip[8] = 64;
    ip[9] = NET_IPPROTO_UDP;
    write_ipv4(ip + 12, source_ip);
    write_ipv4(ip + 16, destination_ip);
    *std_layout_cast<be_uint16_t*>(ip + 10) = ipv4_checksum({ip, 20});

    auto* udp = ip + 20;
    *std_layout_cast<be_uint16_t*>(udp) = source_port;
    *std_layout_cast<be_uint16_t*>(udp + 2) = destination_port;
    *std_layout_cast<be_uint16_t*>(udp + 4) = uint16_t(8 + PAYLOAD_SIZE);
    std::copy(payload.begin(), payload.end(), udp + 8);
    return frame;
}

std::array<uint8_t, 42> make_arp_request(const std::array<uint8_t, 4>& target_ip)
{
    std::array<uint8_t, 42> frame{};
    std::fill_n(frame.begin(), 6, 0xff);
    write_mac(frame.data() + 6, HOST_MAC);
    *std_layout_cast<be_uint16_t*>(frame.data() + 12) = 0x0806;

    auto* arp = frame.data() + 14;
    *std_layout_cast<be_uint16_t*>(arp) = 1;
    *std_layout_cast<be_uint16_t*>(arp + 2) = 0x0800;
    arp[4] = 6;
    arp[5] = 4;
    *std_layout_cast<be_uint16_t*>(arp + 6) = 1;
    write_mac(arp + 8, HOST_MAC);
    write_ipv4(arp + 14, HOST_IP);
    write_ipv4(arp + 24, target_ip);
    return frame;
}

::net_in_addr as_net_ipv4(const std::array<uint8_t, 4>& address)
{
    ::net_in_addr result{};
    std::copy(address.begin(), address.end(), result.s4_addr);
    return result;
}

void add_test_ip_addresses()
{
    auto local = as_net_ipv4(LOCAL_IP);
    auto secondary = as_net_ipv4(SECONDARY_LOCAL_IP);
    zassert_not_null(net_if_ipv4_addr_add(ncm_netif, &local, NET_ADDR_MANUAL, 0),
                     "Failed to add primary test IPv4 identity");
    zassert_not_null(net_if_ipv4_addr_add(ncm_netif, &secondary, NET_ADDR_MANUAL, 0),
                     "Failed to add secondary test IPv4 identity");
}

ssize_t receive_udp(int socket_fd, std::span<uint8_t> buffer, std::chrono::milliseconds timeout)
{
    const int64_t deadline = k_uptime_get() + timeout.count();
    do
    {
        auto received = zsock_recvfrom(socket_fd, buffer.data(), buffer.size(), ZSOCK_MSG_DONTWAIT,
                                       nullptr, nullptr);
        if ((received >= 0) or ((errno != EAGAIN) and (errno != EWOULDBLOCK)))
        {
            return received;
        }
        k_sleep(K_MSEC(1));
    } while (k_uptime_get() < deadline);
    return -ETIMEDOUT;
}

bool wait_for_state(usb::df::cdc::ncm::state expected, std::chrono::milliseconds timeout = 2s)
{
    for (auto start = tick_timer::now(); tick_timer::now() - start < timeout;)
    {
        if (ncm_function->get_state() == expected)
        {
            return true;
        }
        this_thread::sleep_for(1ms);
    }
    return false;
}

void activate_data_interface(uint32_t input_ntb_size = 1024)
{
    zassert_true(host_device->set_configuration(1), "Failed to set configuration");
    constexpr usb::control::request set_input_size{usb::cdc::control::SET_NTB_INPUT_SIZE, 0, 0,
                                                   sizeof(input_ntb_size)};
    zassert_true(host_device->control_out(set_input_size, input_ntb_size),
                 "Failed to set device IN NTB size");
    set_alt_setting(DATA_INTERFACE, 1);
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::ENABLED);
}

void connect_network(ncm_host_peer& peer)
{
    add_test_ip_addresses();
    zassert_equal(net_if_up(ncm_netif), 0, "Failed to bring up the NCM network interface");

    std::array<uint8_t, sizeof(usb::cdc::notification::speed_change) +
                            sizeof(usb::cdc::notification::header)>
        notification{};
    size_t transferred{};
    zassert_equal(peer.receive_notification(notification, transferred), 0,
                  "Failed to receive NCM connection notification");
    zassert_equal(transferred, notification.size(), "NCM connection notification size mismatch");
    zassert_equal(notification[1], uint8_t(usb::cdc::notification::code::CONNECTION_SPEED_CHANGE),
                  "Missing NCM speed-change notification");
    zassert_equal(notification[sizeof(usb::cdc::notification::speed_change) + 1],
                  uint8_t(usb::cdc::notification::code::NETWORK_CONNECTION),
                  "Missing NCM network-connection notification");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::CONNECTED),
                 "NCM function did not reach CONNECTED");
}

struct udp_sender_context
{
    int socket{};
    ::net_sockaddr_in destination{};
    struct k_sem done{};
    std::atomic<int> error{};
    std::atomic<size_t> sent{};
};

struct udp_receiver_context
{
    int socket{};
    struct k_sem done{};
    std::atomic<int> error{};
    std::atomic<size_t> received{};
};

struct blocked_tx_context
{
    const ::device* driver{};
    struct k_sem done{};
    std::atomic<int> error{};
    std::atomic<size_t> completed{};
    size_t target{12};
};

blocked_tx_context blocked_tx{};

void udp_sender(void* context_ptr, void*, void*)
{
    auto& context = *static_cast<udp_sender_context*>(context_ptr);
    std::array<uint8_t, UDP_PAYLOAD_SIZE> payload{};
    for (size_t i = 0; i < TX_DATAGRAM_COUNT; ++i)
    {
        payload[0] = uint8_t(i >> 8);
        payload[1] = uint8_t(i);
        auto result = zsock_sendto(context.socket, payload.data(), payload.size(), 0,
                                   reinterpret_cast<const net_sockaddr*>(&context.destination),
                                   sizeof(context.destination));
        if (result != ssize_t(payload.size()))
        {
            context.error.store(errno == 0 ? -EIO : -errno);
            break;
        }
        context.sent.fetch_add(1);
    }
    k_sem_give(&context.done);
}

void udp_receiver(void* context_ptr, void*, void*)
{
    auto& context = *static_cast<udp_receiver_context*>(context_ptr);
    for (size_t i = 0; i < RX_DATAGRAM_COUNT; ++i)
    {
        std::array<uint8_t, 32> payload{};
        auto received = receive_udp(context.socket, payload, 2s);
        uint16_t expected_sequence = uint16_t(0x100 + i);
        if ((received != 12) or (payload[0] != uint8_t(expected_sequence >> 8)) or
            (payload[1] != uint8_t(expected_sequence)))
        {
            context.error.store(received < 0 ? int(received) : -EBADMSG);
            break;
        }
        context.received.fetch_add(1);
    }
    k_sem_give(&context.done);
}

void blocked_tx_sender(void* context_ptr, void*, void*)
{
    auto& context = *static_cast<blocked_tx_context*>(context_ptr);
    std::array<uint8_t, 200> payload{};
    auto frame = make_udp_frame(payload, LOCAL_IP, HOST_IP, TEST_UDP_PORT, 5001);
    std::copy(HOST_MAC.begin(), HOST_MAC.end(), frame.begin());
    std::copy(DEVICE_MAC.begin(), DEVICE_MAC.end(), frame.begin() + 6);

    for (size_t i = 0; i < context.target; ++i)
    {
        auto* packet =
            net_pkt_alloc_with_buffer(ncm_netif, frame.size(), NET_AF_UNSPEC, 0, K_SECONDS(1));
        if (packet == nullptr)
        {
            context.error.store(-ENOMEM);
            break;
        }
        if (net_pkt_write(packet, frame.data(), frame.size()) != 0)
        {
            net_pkt_unref(packet);
            context.error.store(-EIO);
            break;
        }

        const int result = usb::df::zephyr::cdc_ncm_ethernet::driver_send(context.driver, packet);
        net_pkt_unref(packet);
        if (result != 0)
        {
            context.error.store(result);
            break;
        }
        context.completed.fetch_add(1);
    }
    k_sem_give(&context.done);
}

template <usb::speed SPEED>
const auto& ncm_config()
{
    static const auto config = usb::df::config::make_config(
        usb::df::config::header(usb::df::config::power::bus(200), "ncm-test"),
        ncm_function->config_entry(SPEED, BULK_OUT, BULK_IN, NOTIFY_IN,
                                   usb::endpoint::interval::from_rate(SPEED, 8ms)));
    return config;
}

void initialize_function()
{
    auto* ethernet_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_ncm_eth0));
    zassert_true(device_is_ready(ethernet_dev), "CDC-NCM Ethernet device is not ready");

    if (ncm_function == nullptr)
    {
        ncm_function = usb::df::zephyr::cdc_ncm_ethernet::construct<DT_NODE_FULL_NAME(
            DT_NODELABEL(cdc_ncm_eth0))>(HOST_MAC, {device_ntb_in, device_ntb_out}, ethernet_dev);
        zassert_not_null(ncm_function, "Failed to construct CDC-NCM function");
    }

    ncm_netif = net_if_lookup_by_dev(ethernet_dev);
    zassert_not_null(ncm_netif, "CDC-NCM network interface was not created");
}

void set_alt_setting(uint8_t interface, uint8_t alternate)
{
    int err = usbh_device_interface_set(host_device, interface, alternate, false);
    zassert_equal(err, 0, "SET_INTERFACE(%u, %u) failed: %d", interface, alternate, err);
}

static void* test_setup()
{
    initialize_function();
    udp_sender_started = false;
    zassert_equal(usb::test::host::start(), 0, "Failed to start USB host");

    auto& dev = device().emplace(usb_mac(), PRODUCT_INFO);
    dev.set_config_for_speed(ncm_config<usb::speed::FULL>(), usb::speed::FULL);
    if constexpr (usb::df::zephyr::udc_mac::supported_speeds().includes(usb::speed::HIGH))
    {
        dev.set_config_for_speed(ncm_config<usb::speed::HIGH>(), usb::speed::HIGH);
    }
    dev.open();

    zassert_equal(usb::test::host::bus_resume(), 0, "Failed to issue bus resume");
    zassert_equal(usb::test::host::bus_reset(), 0, "Failed to issue bus reset");
    zassert_equal(usb::test::host::sof_enable(), 0, "Failed to enable SOF generation");
    this_thread::sleep_for(10ms);

    host_device = usb::test::host::wait_for_device();
    zassert_not_null(host_device, "No USB device enumerated on virtual host");
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::DISABLED);
    return host_device;
}

static void reset_test_state()
{
    if (blocked_tx_started)
    {
        if (ncm_function->get_state() != usb::df::cdc::ncm::state::DISABLED)
        {
            set_alt_setting(DATA_INTERFACE, 0);
        }
        if (k_thread_join(&blocked_tx_thread, K_SECONDS(1)) != 0)
        {
            k_thread_abort(&blocked_tx_thread);
            (void)k_thread_join(&blocked_tx_thread, K_SECONDS(1));
        }
        blocked_tx_started = false;
    }
    if ((ncm_netif != nullptr) and net_if_is_admin_up(ncm_netif))
    {
        (void)net_if_down(ncm_netif);
    }
    if (host_device != nullptr)
    {
        auto config = host_device->get_configuration();
        if (config.has_value() and config.value() != 0)
        {
            (void)host_device->set_configuration(0);
        }
    }
    if (udp_socket_fd >= 0)
    {
        (void)zsock_close(udp_socket_fd);
        udp_socket_fd = -1;
    }
    if (ncm_netif != nullptr)
    {
        auto local = as_net_ipv4(LOCAL_IP);
        auto secondary = as_net_ipv4(SECONDARY_LOCAL_IP);
        (void)net_if_ipv4_addr_rm(ncm_netif, &local);
        (void)net_if_ipv4_addr_rm(ncm_netif, &secondary);
    }
    if (udp_sender_started)
    {
        if (k_thread_join(&udp_sender_thread, K_SECONDS(1)) != 0)
        {
            k_thread_abort(&udp_sender_thread);
        }
        udp_sender_started = false;
    }
    if (udp_receiver_started)
    {
        if (k_thread_join(&udp_receiver_thread, K_SECONDS(1)) != 0)
        {
            k_thread_abort(&udp_receiver_thread);
        }
        udp_receiver_started = false;
    }
}

static void test_before(void*)
{
    reset_test_state();
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::DISABLED,
                  "NCM function was not reset between tests");
}

static void test_after(void*)
{
    reset_test_state();
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::DISABLED,
                  "NCM function did not return to DISABLED after test");
}

static void test_teardown(void*)
{
    reset_test_state();
    if (device().has_value())
    {
        device()->close();
        device().reset();
    }
    zassert_equal(usb::test::host::stop(), 0, "Failed to stop USB host");
    host_device = nullptr;
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_data_interface_activation)
{
    zassert_true(host_device->set_configuration(1), "Failed to set configuration");
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::DISABLED);

    set_alt_setting(DATA_INTERFACE, 1);
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::ENABLED);

    set_alt_setting(DATA_INTERFACE, 0);
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::DISABLED);
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_suspend_resume_preserves_disabled_alt)
{
    zassert_true(host_device->set_configuration(1), "Failed to set configuration");
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::DISABLED);

    zassert_equal(usb::test::host::bus_suspend(), 0, "Failed to suspend virtual USB bus");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::SUSPENDED),
                 "NCM function did not enter SUSPENDED");

    zassert_equal(usb::test::host::bus_resume(), 0, "Failed to resume virtual USB bus");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::DISABLED),
                 "NCM function did not preserve DISABLED while data alternate 0 was selected; "
                 "current state=%u",
                 uint8_t(ncm_function->get_state()));
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_control_requests)
{
    zassert_true(host_device->set_configuration(1), "Failed to set configuration");

    auto config_desc = host_device->control_in<usb::standard::descriptor::configuration>(
        {usb::standard::device::GET_DESCRIPTOR,
         uint8_t(usb::standard::descriptor::type::CONFIGURATION) << 8, 0, 512});
    zassert_true(config_desc.has_data(), "Failed to read NCM configuration descriptor");
    zassert_equal(config_desc->wTotalLength, config_desc.as_span().size(),
                  "NCM configuration descriptor length mismatch");

    bool found_iad = false;
    bool found_control_interface = false;
    bool found_data_alt0 = false;
    bool found_data_alt1 = false;
    std::array<uint8_t, 3> endpoint_addresses{};
    size_t endpoint_count{};
    auto desc_set = usb::standard::descriptor_set(config_desc.as_span());
    for (auto it = desc_set.begin(); it != desc_set.end(); ++it)
    {
        zassert_true(it.valid(), "Malformed descriptor in NCM configuration");
        if (auto* iad = it.as<usb::standard::descriptor::interface_association>())
        {
            zassert_equal(iad->bFirstInterface, 0, "NCM IAD starts at the wrong interface");
            zassert_equal(iad->bInterfaceCount, 2, "NCM IAD has the wrong interface count");
            zassert_equal(iad->bFunctionClass, uint8_t(usb::cdc::CLASS_CODE),
                          "NCM IAD has the wrong class");
            zassert_equal(iad->bFunctionSubClass,
                          uint8_t(usb::cdc::subclass::NETWORK_CONTROL_MODEL),
                          "NCM IAD has the wrong subclass");
            found_iad = true;
        }
        else if (auto* iface = it.as<usb::standard::descriptor::interface>())
        {
            if (iface->bInterfaceNumber == 0)
            {
                zassert_equal(iface->bAlternateSetting, 0);
                zassert_equal(iface->bNumEndpoints, 1);
                zassert_equal(iface->bInterfaceClass, uint8_t(usb::cdc::CLASS_CODE));
                zassert_equal(iface->bInterfaceSubClass,
                              uint8_t(usb::cdc::subclass::NETWORK_CONTROL_MODEL));
                found_control_interface = true;
            }
            else if (iface->bInterfaceNumber == DATA_INTERFACE)
            {
                zassert_equal(iface->bInterfaceClass, uint8_t(usb::cdc::data::CLASS_CODE));
                if (iface->bAlternateSetting == 0)
                {
                    zassert_equal(iface->bNumEndpoints, 0);
                    found_data_alt0 = true;
                }
                else if (iface->bAlternateSetting == 1)
                {
                    zassert_equal(iface->bNumEndpoints, 2);
                    found_data_alt1 = true;
                }
                else
                {
                    zassert_true(false, "Unexpected NCM data alternate setting %u",
                                 iface->bAlternateSetting);
                }
            }
            else
            {
                zassert_true(false, "Unexpected NCM interface number %u", iface->bInterfaceNumber);
            }
        }
        else if (auto* endpoint = it.as<usb::standard::descriptor::endpoint>())
        {
            zassert_true(endpoint_count < endpoint_addresses.size(),
                         "Unexpected NCM endpoint descriptor");
            endpoint_addresses[endpoint_count++] = endpoint->bEndpointAddress;
        }
    }
    constexpr std::array<uint8_t, 3> EXPECTED_ENDPOINTS{0x81, 0x02, 0x82};
    zassert_true(found_iad and found_control_interface and found_data_alt0 and found_data_alt1,
                 "NCM configuration is missing a required interface descriptor");
    zassert_equal(endpoint_count, EXPECTED_ENDPOINTS.size(),
                  "NCM configuration has an unexpected endpoint count");
    zassert_true(endpoint_addresses == EXPECTED_ENDPOINTS,
                 "NCM endpoint addresses or descriptor order are incorrect");

    constexpr usb::control::request get_parameters{usb::cdc::control::GET_NTB_PARAMETERS, 0, 0,
                                                   sizeof(usb::cdc::ncm::ntb::parameters)};
    auto parameters = host_device->control_in<usb::cdc::ncm::ntb::parameters>(get_parameters);
    zassert_true(parameters.has_data(), "Failed to read NCM NTB parameters");
    zassert_true(parameters->bmNtbFormatsSupported.ntb16, "NTB16 format is not supported");
    zassert_true(parameters->dwNtbInMaxSize >= usb::cdc::ncm::ntb::min_size<16>(),
                 "Advertised NCM IN NTB size is too small");
    zassert_true(parameters->dwNtbOutMaxSize >= usb::cdc::ncm::ntb::min_size<16>(),
                 "Advertised NCM OUT NTB size is too small");

    uint32_t invalid_size{};
    constexpr usb::control::request set_input_size{usb::cdc::control::SET_NTB_INPUT_SIZE, 0, 0,
                                                   sizeof(invalid_size)};
    zassert_false(host_device->control_out(set_input_size, invalid_size),
                  "NCM accepted a zero input size");
    uint32_t oversized_size = UINT32_MAX;
    zassert_false(host_device->control_out(set_input_size, oversized_size),
                  "NCM accepted an input size larger than its NTB buffer");

    uint32_t accepted_size = 1024;
    zassert_true(host_device->control_out(set_input_size, accepted_size),
                 "NCM rejected a valid input size");

    constexpr usb::control::request get_input_size{usb::cdc::control::GET_NTB_INPUT_SIZE, 0, 0,
                                                   sizeof(uint32_t)};
    auto input_size = host_device->control_in<uint32_t>(get_input_size);
    zassert_true(input_size.has_data(), "Failed to read negotiated NCM input size");
    zassert_equal(**input_size, accepted_size, "NCM did not apply the negotiated input size");

    constexpr usb::control::request wrong_interface_request{
        usb::cdc::control::SET_NTB_INPUT_SIZE, 0, DATA_INTERFACE, sizeof(accepted_size)};
    zassert_false(host_device->control_out(wrong_interface_request, accepted_size),
                  "NCM accepted a class request addressed to its data interface");
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_abort_connect_notification_retries)
{
    ncm_host_peer peer{host_device};
    activate_data_interface();
    add_test_ip_addresses();
    zassert_equal(net_if_up(ncm_netif), 0, "Failed to bring up NCM network interface");

    auto* udc = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));
    bool notification_pending = false;
    for (auto start = tick_timer::now(); tick_timer::now() - start < 2s;)
    {
        if (not udc_ep_queue_is_empty(udc, 0x81))
        {
            notification_pending = true;
            break;
        }
        this_thread::sleep_for(1ms);
    }
    zassert_true(notification_pending, "NCM connect notification was not queued");
    zassert_equal(udc_ep_dequeue(udc, 0x81), 0,
                  "Failed to abort the pending NCM connect notification");

    bool notification_retried = false;
    for (auto start = tick_timer::now(); tick_timer::now() - start < 500ms;)
    {
        if (not udc_ep_queue_is_empty(udc, 0x81))
        {
            notification_retried = true;
            break;
        }
        this_thread::sleep_for(1ms);
    }
    zassert_true(notification_retried,
                 "NCM did not resubmit its connection notification after an aborted callback");

    std::array<uint8_t, sizeof(usb::cdc::notification::speed_change) +
                            sizeof(usb::cdc::notification::header)>
        notification{};
    size_t transferred{};
    zassert_equal(peer.receive_notification(notification, transferred), 0,
                  "Failed to receive retried NCM connection notification");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::CONNECTED),
                 "NCM did not reach CONNECTED after the retried notification");
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_disconnect_notification_abort_retries)
{
    ncm_host_peer peer{host_device};
    activate_data_interface();
    connect_network(peer);

    zassert_equal(net_if_down(ncm_netif), 0, "Failed to bring down NCM network interface");
    auto* udc = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));
    bool disconnect_queued = false;
    for (auto start = tick_timer::now(); tick_timer::now() - start < 1s;)
    {
        if (not udc_ep_queue_is_empty(udc, 0x81))
        {
            disconnect_queued = true;
            break;
        }
        this_thread::sleep_for(1ms);
    }
    zassert_true(disconnect_queued, "NCM disconnect notification was not queued");

    zassert_equal(udc_ep_dequeue(udc, 0x81), 0,
                  "Failed to abort the pending NCM disconnect notification");

    bool disconnect_retried = false;
    for (auto start = tick_timer::now(); tick_timer::now() - start < 500ms;)
    {
        if (not udc_ep_queue_is_empty(udc, 0x81))
        {
            disconnect_retried = true;
            break;
        }
        this_thread::sleep_for(1ms);
    }
    zassert_true(disconnect_retried,
                 "NCM did not resubmit its disconnect notification after an aborted callback");

    std::array<uint8_t, sizeof(usb::cdc::notification::header)> notification{};
    size_t transferred{};
    zassert_equal(peer.receive_notification(notification, transferred), 0,
                  "Failed to receive retried NCM disconnect notification");
    zassert_equal(transferred, notification.size(), "NCM disconnect notification size mismatch");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::ENABLED, 500ms),
                 "NCM did not retry disconnect after an aborted notification");
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_suspend_resume_and_host_reset)
{
    ncm_host_peer peer{host_device};
    activate_data_interface();
    connect_network(peer);

    blocked_tx.driver = DEVICE_DT_GET(DT_NODELABEL(cdc_ncm_eth0));
    blocked_tx.error.store(0);
    blocked_tx.completed.store(0);
    blocked_tx.target = 1;
    k_sem_init(&blocked_tx.done, 0, 1);
    k_thread_create(&blocked_tx_thread, blocked_tx_stack, K_THREAD_STACK_SIZEOF(blocked_tx_stack),
                    blocked_tx_sender, &blocked_tx, nullptr, nullptr, K_PRIO_PREEMPT(5), 0,
                    K_NO_WAIT);
    blocked_tx_started = true;
    zassert_equal(k_sem_take(&blocked_tx.done, K_SECONDS(2)), 0,
                  "Device TX did not queue before bus suspend");
    zassert_equal(k_thread_join(&blocked_tx_thread, K_SECONDS(1)), 0,
                  "Failed to join device TX worker before bus suspend");
    blocked_tx_started = false;
    zassert_equal(blocked_tx.error.load(), 0, "Device TX failed before bus suspend");
    zassert_equal(blocked_tx.completed.load(), 1, "Device TX did not queue one pending NTB");

    zassert_equal(usb::test::host::bus_suspend(), 0, "Failed to suspend virtual USB bus");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::SUSPENDED),
                 "NCM function did not enter SUSPENDED");

    zassert_equal(usb::test::host::bus_resume(), 0, "Failed to resume virtual USB bus");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::CONNECTED),
                 "NCM function did not resume its connected state");

    std::array<std::span<const uint8_t>, 4> pending_datagrams{};
    size_t pending_datagram_count{};
    zassert_equal(peer.receive(pending_datagrams, pending_datagram_count, 2s), 0,
                  "Pending NCM IN NTB was not delivered after bus resume");
    zassert_equal(pending_datagram_count, 1,
                  "Unexpected datagram count in the resumed NCM IN transfer");

    set_alt_setting(DATA_INTERFACE, 0);
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::DISABLED,
                  "Host alternate-setting reset did not disable NCM");
    set_alt_setting(DATA_INTERFACE, 1);
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::ENABLED);

    std::array<uint8_t, sizeof(usb::cdc::notification::speed_change) +
                            sizeof(usb::cdc::notification::header)>
        notification{};
    size_t transferred{};
    zassert_equal(peer.receive_notification(notification, transferred), 0,
                  "Failed to receive reconnect notification after alternate-setting reset");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::CONNECTED),
                 "NCM failed to reconnect after alternate-setting reset");

    int socket_fd = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    zassert_true(socket_fd >= 0, "Failed to create UDP socket after interface reset");
    ::net_sockaddr_in local_address{};
    local_address.sin_family = NET_AF_INET;
    local_address.sin_port = net_htons(TEST_UDP_PORT);
    local_address.sin_addr = as_net_ipv4(SECONDARY_LOCAL_IP);
    zassert_equal(zsock_bind(socket_fd, reinterpret_cast<const net_sockaddr*>(&local_address),
                             sizeof(local_address)),
                  0, "Failed to bind UDP socket after interface reset");

    std::array<uint8_t, 12> payload{};
    payload[0] = 0x5a;
    auto frame = make_udp_frame(payload, HOST_IP, SECONDARY_LOCAL_IP, 7000, TEST_UDP_PORT);
    zassert_true(peer.queue_datagram(frame), "Failed to queue post-reset UDP frame");
    zassert_equal(peer.transmit(), 0, "Failed to transmit post-reset UDP frame");
    std::array<uint8_t, 16> received_payload{};
    auto received = receive_udp(socket_fd, received_payload, 2s);
    zassert_equal(received, 12, "Cancelled NCM OUT receive was not rearmed after reset");
    zassert_equal(received_payload[0], 0x5a, "Post-reset NCM OUT payload was corrupted");
    zassert_equal(zsock_close(socket_fd), 0, "Failed to close post-reset UDP socket");

    zassert_equal(net_if_down(ncm_netif), 0, "Failed to bring down NCM network interface");
    std::array<uint8_t, sizeof(usb::cdc::notification::header)> disconnect_notification{};
    zassert_equal(peer.receive_notification(disconnect_notification, transferred), 0,
                  "Failed to receive NCM disconnect notification");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::ENABLED),
                 "NCM function did not disconnect when the network interface went down");
}

#if 0 // disabled due to unlikely and low impact scenario
ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_size_limit_change_with_pending_in_ntb)
{
    ncm_host_peer peer{host_device};
    activate_data_interface(512);
    connect_network(peer);

    blocked_tx.driver = DEVICE_DT_GET(DT_NODELABEL(cdc_ncm_eth0));
    blocked_tx.error.store(0);
    blocked_tx.completed.store(0);
    blocked_tx.target = 2;
    k_sem_init(&blocked_tx.done, 0, 1);
    k_thread_create(&blocked_tx_thread, blocked_tx_stack, K_THREAD_STACK_SIZEOF(blocked_tx_stack),
                    blocked_tx_sender, &blocked_tx, nullptr, nullptr, K_PRIO_PREEMPT(5), 0,
                    K_NO_WAIT);
    blocked_tx_started = true;

    zassert_equal(k_sem_take(&blocked_tx.done, K_SECONDS(2)), 0,
                  "Could not queue two device NCM IN datagrams");
    zassert_equal(k_thread_join(&blocked_tx_thread, K_SECONDS(1)), 0,
                  "Failed to join the device TX setup worker");
    blocked_tx_started = false;
    zassert_equal(blocked_tx.error.load(), 0, "Failed to queue IN datagrams: %d",
                  blocked_tx.error.load());
    zassert_equal(blocked_tx.completed.load(), blocked_tx.target,
                  "Not all pending IN datagrams were queued");

    uint32_t reduced_size = 128;
    constexpr usb::control::request set_input_size{usb::cdc::control::SET_NTB_INPUT_SIZE, 0, 0,
                                                   sizeof(reduced_size)};
    zassert_true(host_device->control_out(set_input_size, reduced_size),
                 "Host could not lower the negotiated NCM IN NTB size");

    std::array<std::span<const uint8_t>, 4> datagrams{};
    size_t datagram_count{};
    size_t first_ntb_size{};
    zassert_equal(peer.receive(datagrams, datagram_count, 2s, &first_ntb_size), 0,
                  "Failed to receive the already-submitted NCM IN NTB");
    zassert_equal(datagram_count, 1, "First NCM IN NTB contained an unexpected datagram count");

    size_t queued_ntb_size{};
    zassert_equal(peer.receive(datagrams, datagram_count, 2s, &queued_ntb_size), 0,
                  "Failed to receive the pending NCM IN NTB after size renegotiation");
    zassert_true(queued_ntb_size <= reduced_size,
                 "Queued NCM IN NTB exceeds the newly negotiated maximum: %u > %u", queued_ntb_size,
                 reduced_size);
}
#endif

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_alt_reset_wakes_blocked_tx)
{
    ncm_host_peer peer{host_device};
    activate_data_interface(512);
    connect_network(peer);

    blocked_tx.driver = DEVICE_DT_GET(DT_NODELABEL(cdc_ncm_eth0));
    blocked_tx.error.store(0);
    blocked_tx.completed.store(0);
    blocked_tx.target = 12;
    k_sem_init(&blocked_tx.done, 0, 1);
    k_thread_create(&blocked_tx_thread, blocked_tx_stack, K_THREAD_STACK_SIZEOF(blocked_tx_stack),
                    blocked_tx_sender, &blocked_tx, nullptr, nullptr, K_PRIO_PREEMPT(5), 0,
                    K_NO_WAIT);
    blocked_tx_started = true;

    for (auto start = tick_timer::now(); tick_timer::now() - start < 2s;)
    {
        if (blocked_tx.completed.load() >= 2)
        {
            break;
        }
        this_thread::sleep_for(1ms);
    }
    zassert_equal(blocked_tx.completed.load(), 2,
                  "TX worker failed before filling both NCM NTB pages: %d",
                  blocked_tx.error.load());
    zassert_not_equal(k_sem_take(&blocked_tx.done, K_NO_WAIT), 0,
                      "TX worker did not block when NCM IN buffers were full");

    set_alt_setting(DATA_INTERFACE, 0);
    zassert_equal(ncm_function->get_state(), usb::df::cdc::ncm::state::DISABLED);
    zassert_equal(k_sem_take(&blocked_tx.done, K_SECONDS(2)), 0,
                  "Blocked network TX did not wake when host reset the data interface");
    zassert_equal(k_thread_join(&blocked_tx_thread, K_SECONDS(1)), 0,
                  "Failed to join the blocked network TX worker");
    blocked_tx_started = false;
    zassert_equal(blocked_tx.completed.load(), 2,
                  "TX worker sent more data after the NCM interface was disabled");
    zassert_equal(blocked_tx.error.load(), -ENOTCONN,
                  "Blocked TX returned an unexpected error after host reset");

    set_alt_setting(DATA_INTERFACE, 1);
    std::array<uint8_t, sizeof(usb::cdc::notification::speed_change) +
                            sizeof(usb::cdc::notification::header)>
        notification{};
    size_t transferred{};
    zassert_equal(peer.receive_notification(notification, transferred), 0,
                  "Failed to receive NCM reconnect notification after blocked TX reset");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::CONNECTED),
                 "NCM did not reconnect after blocked TX reset");
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_netif_down_wakes_blocked_tx)
{
    ncm_host_peer peer{host_device};
    activate_data_interface(512);
    connect_network(peer);

    blocked_tx.driver = DEVICE_DT_GET(DT_NODELABEL(cdc_ncm_eth0));
    blocked_tx.error.store(0);
    blocked_tx.completed.store(0);
    blocked_tx.target = 12;
    k_sem_init(&blocked_tx.done, 0, 1);
    k_thread_create(&blocked_tx_thread, blocked_tx_stack, K_THREAD_STACK_SIZEOF(blocked_tx_stack),
                    blocked_tx_sender, &blocked_tx, nullptr, nullptr, K_PRIO_PREEMPT(5), 0,
                    K_NO_WAIT);
    blocked_tx_started = true;

    for (auto start = tick_timer::now(); tick_timer::now() - start < 2s;)
    {
        if (blocked_tx.completed.load() >= 2)
        {
            break;
        }
        this_thread::sleep_for(1ms);
    }
    zassert_equal(blocked_tx.completed.load(), 2,
                  "TX worker failed before filling both NCM NTB pages: %d",
                  blocked_tx.error.load());
    zassert_not_equal(k_sem_take(&blocked_tx.done, K_NO_WAIT), 0,
                      "TX worker did not block when NCM IN buffers were full");

    zassert_equal(net_if_down(ncm_netif), 0, "Failed to bring down NCM network interface");
    std::array<uint8_t, sizeof(usb::cdc::notification::header)> notification{};
    size_t transferred{};
    zassert_equal(peer.receive_notification(notification, transferred), 0,
                  "Failed to receive NCM disconnect notification");
    zassert_true(wait_for_state(usb::df::cdc::ncm::state::ENABLED),
                 "NCM did not disconnect after the network interface went down");

    zassert_equal(k_sem_take(&blocked_tx.done, K_SECONDS(2)), 0,
                  "Network-side TX remained blocked after NCM disconnect");
    zassert_equal(k_thread_join(&blocked_tx_thread, K_SECONDS(1)), 0,
                  "Failed to join the network TX worker after disconnect");
    blocked_tx_started = false;
    zassert_equal(blocked_tx.completed.load(), 2,
                  "TX worker sent another datagram after NCM disconnected");
    zassert_equal(blocked_tx.error.load(), -ENOTCONN,
                  "Blocked network TX returned an unexpected disconnect error");
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_in_abort_retries_pending_ntb)
{
    ncm_host_peer peer{host_device};
    activate_data_interface(512);
    connect_network(peer);

    blocked_tx.driver = DEVICE_DT_GET(DT_NODELABEL(cdc_ncm_eth0));
    blocked_tx.error.store(0);
    blocked_tx.completed.store(0);
    blocked_tx.target = 1;
    k_sem_init(&blocked_tx.done, 0, 1);
    k_thread_create(&blocked_tx_thread, blocked_tx_stack, K_THREAD_STACK_SIZEOF(blocked_tx_stack),
                    blocked_tx_sender, &blocked_tx, nullptr, nullptr, K_PRIO_PREEMPT(5), 0,
                    K_NO_WAIT);
    blocked_tx_started = true;
    zassert_equal(k_sem_take(&blocked_tx.done, K_SECONDS(2)), 0,
                  "Device did not queue the pending IN datagram");
    zassert_equal(k_thread_join(&blocked_tx_thread, K_SECONDS(1)), 0,
                  "Failed to join device TX worker");
    blocked_tx_started = false;
    zassert_equal(blocked_tx.error.load(), 0, "Device IN transfer setup failed");

    auto* udc = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));
    zassert_equal(udc_ep_dequeue(udc, 0x82), 0, "Failed to abort the pending NCM bulk IN transfer");
    bool in_transfer_rearmed = false;
    for (auto start = tick_timer::now(); tick_timer::now() - start < 1s;)
    {
        if (not udc_ep_queue_is_empty(udc, 0x82))
        {
            in_transfer_rearmed = true;
            break;
        }
        this_thread::sleep_for(1ms);
    }
    zassert_true(in_transfer_rearmed,
                 "NCM dropped the pending IN NTB after an ECONNABORTED callback");

    std::array<std::span<const uint8_t>, 4> datagrams{};
    size_t datagram_count{};
    zassert_equal(peer.receive(datagrams, datagram_count, 2s), 0,
                  "NCM did not retransmit the aborted IN NTB");
    zassert_equal(datagram_count, 1, "Unexpected datagram count after IN abort recovery");
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_bulk_endpoint_halt_recovery)
{
    ncm_host_peer peer{host_device};
    activate_data_interface(512);
    connect_network(peer);

    int socket_fd = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    zassert_true(socket_fd >= 0, "Failed to create UDP socket: %d", errno);
    udp_socket_fd = socket_fd;
    ::net_sockaddr_in local_address{};
    local_address.sin_family = NET_AF_INET;
    local_address.sin_port = net_htons(TEST_UDP_PORT);
    local_address.sin_addr = as_net_ipv4(SECONDARY_LOCAL_IP);
    zassert_equal(zsock_bind(socket_fd, reinterpret_cast<const net_sockaddr*>(&local_address),
                             sizeof(local_address)),
                  0, "Failed to bind UDP socket for endpoint halt test");

    blocked_tx.driver = DEVICE_DT_GET(DT_NODELABEL(cdc_ncm_eth0));
    blocked_tx.error.store(0);
    blocked_tx.completed.store(0);
    blocked_tx.target = 1;
    k_sem_init(&blocked_tx.done, 0, 1);
    k_thread_create(&blocked_tx_thread, blocked_tx_stack, K_THREAD_STACK_SIZEOF(blocked_tx_stack),
                    blocked_tx_sender, &blocked_tx, nullptr, nullptr, K_PRIO_PREEMPT(5), 0,
                    K_NO_WAIT);
    blocked_tx_started = true;
    zassert_equal(k_sem_take(&blocked_tx.done, K_SECONDS(2)), 0,
                  "Device did not queue the pending IN datagram");
    zassert_equal(k_thread_join(&blocked_tx_thread, K_SECONDS(1)), 0,
                  "Failed to join device TX worker");
    blocked_tx_started = false;
    zassert_equal(blocked_tx.error.load(), 0, "Device IN transfer setup failed");

    auto* udc = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));
    std::array<std::span<const uint8_t>, 4> datagrams{};
    size_t datagram_count{};
    int err = udc_ep_set_halt(udc, 0x82);
    zassert_equal(err, 0, "Failed to halt NCM bulk IN endpoint in UDC");
    err = peer.receive(datagrams, datagram_count, 100ms);
    zassert_not_equal(err, 0, "Halted NCM bulk IN endpoint returned data");
    err = udc_ep_clear_halt(udc, 0x82);
    zassert_equal(err, 0, "Failed to clear NCM bulk IN halt in UDC");
    zassert_equal(peer.receive(datagrams, datagram_count, 2s), 0,
                  "Pending NCM IN NTB did not recover after clear-halt");

    std::array<uint8_t, 12> rejected_payload{};
    auto rejected_frame =
        make_udp_frame(rejected_payload, HOST_IP, SECONDARY_LOCAL_IP, 7100, TEST_UDP_PORT);
    zassert_true(peer.queue_datagram(rejected_frame), "Failed to queue halted OUT datagram");
    err = udc_ep_set_halt(udc, 0x02);
    zassert_equal(err, 0, "Failed to halt NCM bulk OUT endpoint in UDC");
    zassert_not_equal(peer.transmit(), 0, "Halted NCM bulk OUT endpoint accepted data");

    err = udc_ep_clear_halt(udc, 0x02);
    zassert_equal(err, 0, "Failed to clear NCM bulk OUT halt in UDC");

    err = udc_ep_dequeue(udc, 0x02);
    zassert_equal(err, 0, "Failed to cancel pending NCM bulk OUT receives");
    bool out_receive_rearmed = false;
    for (auto start = tick_timer::now(); tick_timer::now() - start < 1s;)
    {
        if (not udc_ep_queue_is_empty(udc, 0x02))
        {
            out_receive_rearmed = true;
            break;
        }
        this_thread::sleep_for(1ms);
    }
    zassert_true(out_receive_rearmed,
                 "NCM did not rearm bulk OUT after an aborted receive callback");

    std::array<uint8_t, 12> accepted_payload{};
    accepted_payload[0] = 0xa5;
    auto accepted_frame =
        make_udp_frame(accepted_payload, HOST_IP, SECONDARY_LOCAL_IP, 7101, TEST_UDP_PORT);
    zassert_true(peer.queue_datagram(accepted_frame), "Failed to queue recovered OUT datagram");
    zassert_equal(peer.transmit(), 0, "NCM bulk OUT did not recover after clear-halt");
    std::array<uint8_t, 16> received_payload{};
    auto received = receive_udp(socket_fd, received_payload, 2s);
    zassert_equal(received, 12, "Recovered NCM OUT datagram did not reach UDP");
    zassert_equal(received_payload[0], 0xa5, "Recovered NCM OUT payload was corrupted");
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_in_full_packet_zlp_completion)
{
    ncm_host_peer peer{host_device};
    activate_data_interface(1024);
    connect_network(peer);

    std::array<uint8_t, 442> payload{};
    auto frame = make_udp_frame(payload, LOCAL_IP, HOST_IP, TEST_UDP_PORT, 5001);
    std::copy(HOST_MAC.begin(), HOST_MAC.end(), frame.begin());
    std::copy(DEVICE_MAC.begin(), DEVICE_MAC.end(), frame.begin() + 6);

    auto* packet = net_pkt_alloc_with_buffer(ncm_netif, frame.size(), NET_AF_UNSPEC, NET_IPPROTO_IP,
                                             K_SECONDS(1));
    zassert_not_null(packet, "Failed to allocate full-packet NCM test frame");
    zassert_equal(net_pkt_write(packet, frame.data(), frame.size()), 0,
                  "Failed to write full-packet NCM test frame");
    int err = usb::df::zephyr::cdc_ncm_ethernet::driver_send(
        DEVICE_DT_GET(DT_NODELABEL(cdc_ncm_eth0)), packet);
    net_pkt_unref(packet);
    zassert_equal(err, 0, "Failed to queue full-packet NCM IN transfer");

    std::array<std::span<const uint8_t>, 4> datagrams{};
    size_t datagram_count{};
    size_t ntb_size{};
    zassert_equal(peer.receive(datagrams, datagram_count, 2s, &ntb_size), 0,
                  "Full-packet NCM IN transfer did not complete after its trailing ZLP");
    zassert_equal(ntb_size, 512, "Full-packet NCM transfer had an unexpected NTB size");
    zassert_equal(datagram_count, 1, "Full-packet NCM NTB had an unexpected datagram count");
    zassert_equal(datagrams[0].size(), frame.size(),
                  "Full-packet NCM datagram length did not survive the trailing ZLP");
}

ZTEST(c2usb_usb_cdc_ncm_virtual, test_ncm_udp_rx_tx_and_backpressure)
{
    ncm_host_peer peer{host_device};
    activate_data_interface(512);
    connect_network(peer);

    int socket_fd = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    zassert_true(socket_fd >= 0, "Failed to create UDP socket: %d", errno);
    udp_socket_fd = socket_fd;

    ::net_sockaddr_in local_address{};
    local_address.sin_family = NET_AF_INET;
    local_address.sin_port = net_htons(TEST_UDP_PORT);
    local_address.sin_addr = as_net_ipv4(SECONDARY_LOCAL_IP);
    zassert_equal(zsock_bind(socket_fd, reinterpret_cast<const net_sockaddr*>(&local_address),
                             sizeof(local_address)),
                  0, "Failed to bind UDP socket to secondary IPv4 identity");

    std::array<uint8_t, 12> rejected_payload{};
    auto malformed_frame =
        make_udp_frame(rejected_payload, HOST_IP, SECONDARY_LOCAL_IP, 4999, TEST_UDP_PORT);
    zassert_equal(peer.transmit_malformed(malformed_frame), 0,
                  "Failed to transmit malformed NCM NTB");
    std::array<uint8_t, 32> discarded_payload{};
    zassert_equal(receive_udp(socket_fd, discarded_payload, 50ms), -ETIMEDOUT,
                  "UDP stack accepted a datagram from malformed NCM NTB");

    for (size_t i = 0; i < RX_DATAGRAM_COUNT; ++i)
    {
        std::array<uint8_t, 12> payload{};
        payload[0] = uint8_t(i >> 8);
        payload[1] = uint8_t(i);
        auto frame = make_udp_frame(payload, HOST_IP, SECONDARY_LOCAL_IP, 5000 + i, TEST_UDP_PORT);
        zassert_true(peer.queue_datagram(frame), "Failed to queue host UDP datagram %u", i);
    }
    zassert_equal(peer.transmit(), 0, "Failed to transmit host NCM NTB");

    for (size_t i = 0; i < RX_DATAGRAM_COUNT; ++i)
    {
        std::array<uint8_t, 32> payload{};
        auto received = receive_udp(socket_fd, payload, 2s);
        zassert_equal(received, 12, "UDP socket received an unexpected payload length");
        zassert_equal(payload[0], uint8_t(i >> 8), "UDP payload sequence high byte mismatch");
        zassert_equal(payload[1], uint8_t(i), "UDP payload sequence low byte mismatch");
    }

    auto arp_request = make_arp_request(SECONDARY_LOCAL_IP);
    zassert_true(peer.queue_datagram(arp_request), "Failed to queue host ARP request");
    zassert_equal(peer.transmit(), 0, "Failed to transmit host ARP request");

    std::array<std::span<const uint8_t>, 16> received_datagrams{};
    size_t received_count{};
    bool received_arp_reply = false;
    for (size_t attempt = 0; attempt < 4 and not received_arp_reply; ++attempt)
    {
        int err = peer.receive(received_datagrams, received_count, 2s);
        zassert_equal(err, 0, "Failed to receive NCM response to ARP request: %d", err);
        for (size_t i = 0; i < received_count; ++i)
        {
            auto frame = received_datagrams[i];
            if ((frame.size() >= 42) and
                (*std_layout_cast<const be_uint16_t*>(frame.data() + 12) == 0x0806) and
                (*std_layout_cast<const be_uint16_t*>(frame.data() + 20) == 2))
            {
                received_arp_reply = true;
            }
        }
    }
    zassert_true(received_arp_reply, "Device did not return an ARP reply over NCM");

    udp_sender_context sender{};
    sender.socket = socket_fd;
    sender.destination.sin_family = NET_AF_INET;
    sender.destination.sin_port = net_htons(5001);
    sender.destination.sin_addr = as_net_ipv4(HOST_IP);
    k_sem_init(&sender.done, 0, 1);
    k_thread_create(&udp_sender_thread, udp_sender_stack, K_THREAD_STACK_SIZEOF(udp_sender_stack),
                    udp_sender, &sender, nullptr, nullptr, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
    udp_sender_started = true;

    udp_receiver_context receiver{};
    receiver.socket = socket_fd;
    k_sem_init(&receiver.done, 0, 1);
    k_thread_create(&udp_receiver_thread, udp_receiver_stack,
                    K_THREAD_STACK_SIZEOF(udp_receiver_stack), udp_receiver, &receiver, nullptr,
                    nullptr, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
    udp_receiver_started = true;

    for (size_t i = 0; i < RX_DATAGRAM_COUNT; ++i)
    {
        std::array<uint8_t, 12> payload{};
        uint16_t sequence = uint16_t(0x100 + i);
        payload[0] = uint8_t(sequence >> 8);
        payload[1] = uint8_t(sequence);
        auto frame = make_udp_frame(payload, HOST_IP, SECONDARY_LOCAL_IP, 6000 + i, TEST_UDP_PORT);
        zassert_true(peer.queue_datagram(frame), "Failed to queue concurrent UDP datagram %u", i);
    }
    zassert_equal(peer.transmit(), 0, "Failed to transmit concurrent host NCM traffic");

    std::array<bool, TX_DATAGRAM_COUNT> seen{};
    size_t device_datagram_count{};
    size_t ntb_count{};
    while (device_datagram_count < TX_DATAGRAM_COUNT)
    {
        zassert_equal(peer.receive(received_datagrams, received_count, 3s), 0,
                      "Timed out while draining device NCM IN traffic");
        ++ntb_count;
        for (size_t i = 0; i < received_count; ++i)
        {
            auto frame = received_datagrams[i];
            zassert_true(frame.size() >= 14 + 20 + 8 + UDP_PAYLOAD_SIZE,
                         "Device sent a truncated IPv4 UDP frame");
            zassert_equal(*std_layout_cast<const be_uint16_t*>(frame.data() + 12), 0x0800,
                          "Device sent a non-IPv4 frame during UDP stress");
            const auto* ip = frame.data() + 14;
            zassert_equal(ip[9], NET_IPPROTO_UDP, "Device sent a non-UDP IPv4 packet");
            const size_t ip_header_length = size_t(ip[0] & 0x0f) * 4;
            const auto* udp = ip + ip_header_length;
            const auto* payload = udp + 8;
            const size_t sequence = size_t(payload[0]) << 8 | payload[1];
            zassert_true(sequence < TX_DATAGRAM_COUNT, "Unexpected UDP sequence %u", sequence);
            zassert_false(seen[sequence], "Duplicate UDP sequence %u", sequence);
            seen[sequence] = true;
            ++device_datagram_count;
        }
    }

    zassert_equal(k_sem_take(&sender.done, K_SECONDS(3)), 0,
                  "Device-side UDP sender remained blocked after host drained NCM IN");
    zassert_equal(k_sem_take(&receiver.done, K_SECONDS(3)), 0,
                  "Concurrent host-to-device UDP receiver remained blocked");
    zassert_equal(receiver.error.load(), 0, "Concurrent UDP receive failed: %d",
                  receiver.error.load());
    zassert_equal(receiver.received.load(), RX_DATAGRAM_COUNT,
                  "Concurrent UDP receiver missed datagrams");
    zassert_equal(sender.error.load(), 0, "Device-side UDP send failed: %d", sender.error.load());
    zassert_equal(sender.sent.load(), TX_DATAGRAM_COUNT, "Not all UDP datagrams were submitted");
    zassert_true(ntb_count >= TX_DATAGRAM_COUNT / 2,
                 "NCM TX pressure test produced too few small NTBs: %u", ntb_count);
    zassert_equal(zsock_close(socket_fd), 0, "Failed to close UDP socket");
    udp_socket_fd = -1;
}

} // namespace

extern "C" void c2usb_cdc_ncm_ethernet_get_mac([[maybe_unused]] const ::device* dev,
                                               uint8_t* mac_address)
{
    std::copy(DEVICE_MAC.begin(), DEVICE_MAC.end(), mac_address);
}

ZTEST_SUITE(c2usb_usb_cdc_ncm_virtual, nullptr, test_setup, test_before, test_after, test_teardown);
