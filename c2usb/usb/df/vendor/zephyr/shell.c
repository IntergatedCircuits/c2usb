#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

struct shell_transport c2usb_shell_transport = {
    .api = NULL,
    .ctx = NULL,
};

SHELL_DEFINE(shell_c2usb, CONFIG_C2USB_SHELL_PROMPT, &c2usb_shell_transport,
             CONFIG_C2USB_SHELL_BACKEND_LOG_MESSAGE_QUEUE_SIZE,
             CONFIG_C2USB_SHELL_BACKEND_LOG_MESSAGE_QUEUE_TIMEOUT, SHELL_FLAG_OLF_CRLF);

const struct shell* c2usb_shell_handle()
{
    return &shell_c2usb;
}
