#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "tusb.h"

/* Development/test identifiers */
#define USB_VID 0xCAFE
#define USB_PID 0x4111
#define USB_BCD 0x0100

/* --------------------------------------------------------------------------
 * Device descriptor
 * -------------------------------------------------------------------------- */

static tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,

    .bcdUSB             = 0x0200,

    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = USB_BCD,

    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,

    .bNumConfigurations = 1
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

/* --------------------------------------------------------------------------
 * Configuration descriptor
 * -------------------------------------------------------------------------- */

enum
{
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static uint8_t const desc_configuration[] =
{
    TUD_CONFIG_DESCRIPTOR(
        1,                  /* configuration number */
        ITF_NUM_TOTAL,      /* interface count */
        0,                  /* string index */
        CONFIG_TOTAL_LEN,
        0x00,               /* bus powered */
        100                 /* mA */
    ),

    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC,
        4,                  /* string index */
        EPNUM_CDC_NOTIF,
        8,
        EPNUM_CDC_OUT,
        EPNUM_CDC_IN,
        64
    )
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

/* --------------------------------------------------------------------------
 * String descriptors
 * -------------------------------------------------------------------------- */

static char const *string_desc_arr[] =
{
    (const char[]){ 0x09, 0x04 }, /* English 0x0409 */
    "UTFPR",
    "BlackPill F411 CMSIS CDC",
    "F411CDC01",
    "USB CDC"
};

static uint16_t desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    size_t count;

    if (index == 0)
    {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        count = 1;
    }
    else
    {
        const size_t n_strings =
            sizeof(string_desc_arr) / sizeof(string_desc_arr[0]);

        if (index >= n_strings)
        {
            return NULL;
        }

        const char *str = string_desc_arr[index];
        count = strlen(str);

        if (count > 32)
        {
            count = 32;
        }

        for (size_t i = 0; i < count; i++)
        {
            desc_str[1 + i] = (uint16_t)str[i];
        }
    }

    desc_str[0] =
        (uint16_t)((TUSB_DESC_STRING << 8) | (2U * count + 2U));

    return desc_str;
}
