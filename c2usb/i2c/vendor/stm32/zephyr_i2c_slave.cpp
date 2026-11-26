// SPDX-License-Identifier: MPL-2.0
#include "i2c/vendor/stm32/zephyr_i2c_slave.hpp"
#include <array>
#include <new>
#include <optional>
#include "i2c/vendor/stm32/hal_i2c_slave.hpp"
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/i2c/i2c.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
extern "C"
{
#include <zephyr/drivers/dma/dma_stm32.h>
}
#include <stm32_ll_dma.h>

LOG_MODULE_REGISTER(c2usb_i2c_stm32, CONFIG_C2USB_I2C_STM32_LOG_LEVEL);

// the SoC's I2C IP version determines the applicable compatible
#if C2USB_STM32_I2C_V2
#define DT_DRV_COMPAT c2usb_stm32_i2c_v2_target
BUILD_ASSERT(!DT_HAS_COMPAT_STATUS_OKAY(c2usb_stm32_i2c_v1_target),
             "The SoC has I2C v2 IP, use the c2usb,stm32-i2c-v2-target compatible");
#else
#define DT_DRV_COMPAT c2usb_stm32_i2c_v1_target
BUILD_ASSERT(!DT_HAS_COMPAT_STATUS_OKAY(c2usb_stm32_i2c_v2_target),
             "The SoC has I2C v1 IP, use the c2usb,stm32-i2c-v1-target compatible");
#endif

// the DMA buffers are provided by the application / c2usb classes without cache maintenance
BUILD_ASSERT(!IS_ENABLED(CONFIG_DCACHE),
             "c2usb STM32 I2C target driver doesn't support data cache maintenance");

#if defined(CONFIG_DMAMUX_STM32)
#error "c2usb STM32 I2C target driver doesn't support DMAMUX yet"
#endif

namespace i2c::stm32::zephyr
{
struct dma_channel
{
    const ::device* dev;
    uintptr_t base;
    uint32_t channel;
    uint32_t slot;
    uint32_t channel_config;
};

struct config
{
    uintptr_t base;
    const stm32_pclken* pclken;
    size_t pclk_len;
    const pinctrl_dev_config* pcfg;
    gpio_dt_spec int_gpio;
    uint32_t bitrate;
    /// v2 IP timing presets, in triplets of <periph_clock bitrate timing>
    std::span<const uint32_t> timings;
    dma_channel tx_dma;
    dma_channel rx_dma;
    void (*irq_config)();
};

/// @brief  Zephyr integration of the STM32 HAL I2C slave driver.
///         The HAL handles are owned by the driver, they are constructed first (base-from-member).
class i2c_slave final : private hal_handles, public hal_i2c_slave
{
  public:
    static int init(const ::device* dev);

  private:
    i2c_slave(const config& cfg, uint32_t timing);

    void set_pin_interrupt(bool asserted) override;

    const config& config_;
};

namespace
{
/// @brief  The Zephyr device API pointer of this driver,
///         only used to identify the devices of this driver.
constexpr int device_api_marker = 0;

/// @brief  Selects the HAL timing parameter for the current peripheral clock rate.
std::optional<uint32_t> get_timing(const config& cfg, [[maybe_unused]] uint32_t clock)
{
#if C2USB_STM32_I2C_V2
    // the TIMINGR value is computed for a specific clock rate and bitrate
    for (size_t i = 0; (i + 2) < cfg.timings.size(); i += 3)
    {
        if ((cfg.timings[i] == clock) and (cfg.timings[i + 1] == cfg.bitrate))
        {
            return cfg.timings[i + 2];
        }
    }
    return std::nullopt;
#else
    // the HAL derives the configuration from the bitrate and the APB1 clock rate
    return cfg.bitrate;
#endif
}

void dma_callback([[maybe_unused]] const ::device* dev, void* user_data,
                  [[maybe_unused]] uint32_t channel, [[maybe_unused]] int status)
{
    // the HAL handles the DMA flags and status on its own
    HAL_DMA_IRQHandler(static_cast<DMA_HandleTypeDef*>(user_data));
}

/// @brief  Reserves the DMA channel in the Zephyr DMA driver, which routes the channel's
///         interrupt to the HAL DMA driver, and configures the HAL DMA handle.
int setup_dma(const dma_channel& ch, DMA_HandleTypeDef& hdma)
{
    if (!device_is_ready(ch.dev))
    {
        LOG_ERR("DMA device %s not ready", ch.dev->name);
        return -ENODEV;
    }

    struct dma_config cfg{};
    cfg.dma_slot = ch.slot;
    cfg.channel_direction = STM32_DMA_CONFIG_DIRECTION(ch.channel_config);
    cfg.channel_priority = STM32_DMA_CONFIG_PRIORITY(ch.channel_config);
    const auto periph_size = STM32_DMA_CONFIG_PERIPHERAL_DATA_SIZE(ch.channel_config);
    const auto mem_size = STM32_DMA_CONFIG_MEMORY_DATA_SIZE(ch.channel_config);
    const auto periph_adj = STM32_DMA_CONFIG_PERIPHERAL_ADDR_INC(ch.channel_config)
                                ? DMA_ADDR_ADJ_INCREMENT
                                : DMA_ADDR_ADJ_NO_CHANGE;
    const auto mem_adj = STM32_DMA_CONFIG_MEMORY_ADDR_INC(ch.channel_config)
                             ? DMA_ADDR_ADJ_INCREMENT
                             : DMA_ADDR_ADJ_NO_CHANGE;
    uint16_t src_adj{};
    uint16_t dst_adj{};
    if (cfg.channel_direction == MEMORY_TO_PERIPHERAL)
    {
        cfg.source_data_size = mem_size;
        cfg.dest_data_size = periph_size;
        src_adj = mem_adj;
        dst_adj = periph_adj;
    }
    else if (cfg.channel_direction == PERIPHERAL_TO_MEMORY)
    {
        cfg.source_data_size = periph_size;
        cfg.dest_data_size = mem_size;
        src_adj = periph_adj;
        dst_adj = mem_adj;
    }
    else
    {
        LOG_ERR("DMA channel %u invalid direction", ch.channel);
        return -EINVAL;
    }
    cfg.source_burst_length = cfg.source_data_size;
    cfg.dest_burst_length = cfg.dest_data_size;
    cfg.dma_callback = dma_callback;
    cfg.user_data = &hdma;
    // inform the Zephyr DMA driver that the channel is managed by the HAL
    cfg.linked_channel = STM32_DMA_HAL_OVERRIDE;

    int ret = dma_config(ch.dev, ch.channel, &cfg);
    if (ret != 0)
    {
        LOG_ERR("DMA channel %u config failed: %d", ch.channel, ret);
        return ret;
    }

    ret = dma_stm32_zcfg_to_halcfg(ch.dev, &cfg, &hdma.Init, src_adj, dst_adj);
    if (ret != 0)
    {
        LOG_ERR("DMA channel %u HAL config failed: %d", ch.channel, ret);
        return ret;
    }
    // NOLINTNEXTLINE(*-int-to-ptr, *-reinterpret-cast)
    auto* dma_base = reinterpret_cast<DMA_TypeDef*>(ch.base);
    hdma.Instance = STM32_DMA_GET_INSTANCE(dma_base, ch.channel);
    return 0;
}

i2c_slave& slave_from_isr_arg(const void* arg)
{
    return *std::launder(static_cast<i2c_slave*>(const_cast<void*>(arg)));
}

void ev_isr(const void* arg)
{
    slave_from_isr_arg(arg).ev_irq_handler();
}

[[maybe_unused]] void er_isr(const void* arg)
{
    slave_from_isr_arg(arg).er_irq_handler();
}
} // namespace

i2c_slave::i2c_slave(const config& cfg, uint32_t timing)
    // NOLINTNEXTLINE(*-int-to-ptr, *-reinterpret-cast)
    : hal_handles(reinterpret_cast<I2C_TypeDef*>(cfg.base), timing),
      hal_i2c_slave(hal_handles::i2c),
      config_(cfg)
{}

i2c::slave* get_slave(const ::device* dev)
{
    if ((dev == nullptr) or (dev->api != &device_api_marker) or !device_is_ready(dev))
    {
        return nullptr;
    }
    return std::launder(static_cast<i2c_slave*>(dev->data));
}

void i2c_slave::set_pin_interrupt(bool asserted)
{
    // the pin's active level is configured in the devicetree
    gpio_pin_set_dt(&config_.int_gpio, asserted);
}

int i2c_slave::init(const ::device* dev)
{
    const auto& cfg = *static_cast<const config*>(dev->config);
    const auto* clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
    // NOLINTBEGIN(*-const-cast)
    auto* bus_clk = const_cast<stm32_pclken*>(&cfg.pclken[0]);
    auto* kernel_clk = const_cast<stm32_pclken*>(&cfg.pclken[cfg.pclk_len - 1]);
    // NOLINTEND(*-const-cast)

    if (!gpio_is_ready_dt(&cfg.int_gpio))
    {
        LOG_ERR("INT GPIO not ready");
        return -ENODEV;
    }
    int ret = gpio_pin_configure_dt(&cfg.int_gpio, GPIO_OUTPUT_INACTIVE);
    if (ret != 0)
    {
        return ret;
    }

    ret = pinctrl_apply_state(cfg.pcfg, PINCTRL_STATE_DEFAULT);
    if (ret != 0)
    {
        LOG_ERR("I2C pinctrl setup failed: %d", ret);
        return ret;
    }

    if (!device_is_ready(clk))
    {
        return -ENODEV;
    }
    ret = clock_control_on(clk, bus_clk);
    if ((ret == 0) and (cfg.pclk_len > 1))
    {
        ret = clock_control_configure(clk, kernel_clk, nullptr);
    }
    uint32_t clock_rate{};
    if (ret == 0)
    {
        ret = clock_control_get_rate(clk, kernel_clk, &clock_rate);
    }
    if (ret != 0)
    {
        LOG_ERR("I2C clock setup failed: %d", ret);
        return ret;
    }

    auto timing = get_timing(cfg, clock_rate);
    if (!timing.has_value())
    {
        LOG_ERR("No I2C timings entry for %u Hz bitrate at %u Hz clock", cfg.bitrate, clock_rate);
        return -EINVAL;
    }

    auto* self = new (dev->data) i2c_slave(cfg, timing.value());

    ret = setup_dma(cfg.tx_dma, self->dma_tx);
    if (ret == 0)
    {
        ret = setup_dma(cfg.rx_dma, self->dma_rx);
    }
    if (ret != 0)
    {
        return ret;
    }
    if (!self->init_dma())
    {
        LOG_ERR("HAL DMA init failed");
        return -EIO;
    }

    cfg.irq_config();
    return 0;
}
} // namespace i2c::stm32::zephyr

using i2c::stm32::zephyr::i2c_slave;
// IRQ_CONNECT requires unqualified ISR names
using i2c::stm32::zephyr::er_isr;
using i2c::stm32::zephyr::ev_isr;

#define C2USB_DMA_IRQ_PRIO_BY_IDX(idx, node_id) DT_IRQ_BY_IDX(node_id, idx, priority)

// the DMA controller has one interrupt entry per channel / stream
#define C2USB_DMA_IRQ_PRIOS(node_id)                                                               \
    std::array<uint32_t, DT_NUM_IRQS(node_id)>                                                     \
    {                                                                                              \
        LISTIFY(DT_NUM_IRQS(node_id), C2USB_DMA_IRQ_PRIO_BY_IDX, (, ), node_id)                    \
    }

#define C2USB_I2C_STM32_DMA_IRQ_PRIO(inst, name)                                                   \
    C2USB_DMA_IRQ_PRIOS(DT_INST_DMAS_CTLR_BY_NAME(inst, name))                                     \
    [DT_INST_DMAS_CELL_BY_NAME(inst, name, channel) - STM32_DMA_STREAM_OFFSET]

#define C2USB_I2C_STM32_EV_IRQ_NAME(inst)                                                          \
    COND_CODE_1(DT_INST_IRQ_HAS_NAME(inst, combined), (combined), (event))

// the HAL I2C state machine is driven by both the I2C and the DMA interrupts,
// they must not preempt each other
#define C2USB_I2C_STM32_IRQ_PRIO_CHECK(inst, irq_name)                                             \
    BUILD_ASSERT((DT_INST_IRQ_BY_NAME(inst, irq_name, priority) ==                                 \
                  C2USB_I2C_STM32_DMA_IRQ_PRIO(inst, tx)) &&                                       \
                     (DT_INST_IRQ_BY_NAME(inst, irq_name, priority) ==                             \
                      C2USB_I2C_STM32_DMA_IRQ_PRIO(inst, rx)),                                     \
                 "I2C and DMA IRQ priorities must be identical");

#define C2USB_I2C_STM32_IRQ_CONNECT(inst, irq_name, isr)                                           \
    IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, irq_name, irq),                                          \
                DT_INST_IRQ_BY_NAME(inst, irq_name, priority), isr, &c2usb_i2c_stm32_data_##inst,  \
                0);                                                                                \
    irq_enable(DT_INST_IRQ_BY_NAME(inst, irq_name, irq));

#define C2USB_I2C_STM32_DMA_CHANNEL(inst, name)                                                    \
    {                                                                                              \
        .dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, name)),                               \
        .base = DT_REG_ADDR(DT_INST_DMAS_CTLR_BY_NAME(inst, name)),                                \
        .channel = DT_INST_DMAS_CELL_BY_NAME(inst, name, channel),                                 \
        .slot = STM32_DT_INST_DMA_SLOT(inst, name),                                                \
        .channel_config = STM32_DT_INST_DMA_CHANNEL_CONFIG(inst, name),                            \
    }

#define C2USB_I2C_STM32_INT_GPIO(child) GPIO_DT_SPEC_GET(child, int_gpios)

#define C2USB_I2C_STM32_TIMINGS_DEFINE(inst)                                                       \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, timings),                                              \
                (static constexpr uint32_t c2usb_i2c_stm32_timings_##inst[] =                      \
                     DT_INST_PROP(inst, timings);),                                                \
                ())

#define C2USB_I2C_STM32_TIMINGS(inst)                                                              \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, timings),                                              \
                (std::span<const uint32_t>(c2usb_i2c_stm32_timings_##inst)),                       \
                (std::span<const uint32_t>()))

#define C2USB_I2C_STM32_DEFINE(inst)                                                               \
    BUILD_ASSERT(DT_CHILD_NUM_STATUS_OKAY(DT_DRV_INST(inst)) == 1,                                 \
                 "Exactly one I2C target function (child node) is supported");                     \
    C2USB_I2C_STM32_IRQ_PRIO_CHECK(inst, C2USB_I2C_STM32_EV_IRQ_NAME(inst))                        \
    IF_DISABLED(DT_INST_IRQ_HAS_NAME(inst, combined),                                              \
                (C2USB_I2C_STM32_IRQ_PRIO_CHECK(inst, error)))                                     \
    PINCTRL_DT_INST_DEFINE(inst);                                                                  \
    static const stm32_pclken c2usb_i2c_stm32_pclken_##inst[] = STM32_DT_INST_CLOCKS(inst);        \
    C2USB_I2C_STM32_TIMINGS_DEFINE(inst)                                                           \
    alignas(i2c_slave) static uint8_t c2usb_i2c_stm32_data_##inst[sizeof(i2c_slave)];              \
    static void c2usb_i2c_stm32_irq_config_##inst()                                                \
    {                                                                                              \
        C2USB_I2C_STM32_IRQ_CONNECT(inst, C2USB_I2C_STM32_EV_IRQ_NAME(inst), ev_isr)               \
        IF_DISABLED(DT_INST_IRQ_HAS_NAME(inst, combined),                                          \
                    (C2USB_I2C_STM32_IRQ_CONNECT(inst, error, er_isr)))                            \
    }                                                                                              \
    static const i2c::stm32::zephyr::config c2usb_i2c_stm32_config_##inst = {                      \
        .base = DT_INST_REG_ADDR(inst),                                                            \
        .pclken = c2usb_i2c_stm32_pclken_##inst,                                                   \
        .pclk_len = DT_INST_NUM_CLOCKS(inst),                                                      \
        .pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                              \
        .int_gpio = DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, C2USB_I2C_STM32_INT_GPIO),             \
        .bitrate = DT_INST_PROP_OR(inst, clock_frequency, I2C_BITRATE_STANDARD),                   \
        .timings = C2USB_I2C_STM32_TIMINGS(inst),                                                  \
        .tx_dma = C2USB_I2C_STM32_DMA_CHANNEL(inst, tx),                                           \
        .rx_dma = C2USB_I2C_STM32_DMA_CHANNEL(inst, rx),                                           \
        .irq_config = c2usb_i2c_stm32_irq_config_##inst,                                           \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(inst, i2c_slave::init, nullptr, c2usb_i2c_stm32_data_##inst,             \
                          &c2usb_i2c_stm32_config_##inst, POST_KERNEL,                             \
                          CONFIG_C2USB_I2C_STM32_INIT_PRIORITY,                                    \
                          &i2c::stm32::zephyr::device_api_marker);

DT_INST_FOREACH_STATUS_OKAY(C2USB_I2C_STM32_DEFINE)
