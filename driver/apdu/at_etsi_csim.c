#include "at_cmd.h"

#include <cjson-ext/cJSON_ex.h>
#include <euicc/hexutil.h>
#include <euicc/interface.h>
#include <lpac/utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "at_helpers.h"
#include "driver.h"

#include <stdarg.h>

static const char *at_csim_sw_name(const uint8_t sw1, const uint8_t sw2) {
    switch (sw1) {
    case 0x61:
        return "bytes still pending";
    case 0x62:
        return "warning, state unchanged";
    case 0x69:
        switch (sw2) {
        case 0x82:
            return "security status not satisfied";
        case 0x85:
            return "condition of use not satisfied";
        }
        break;
    case 0x6A:
        switch (sw2) {
        case 0x81:
            return "no logical channel available (pool exhausted by leaked sessions?)";
        case 0x82:
            return "file or application not found";
        case 0x86:
            return "incorrect parameters P1-P2";
        }
        break;
    case 0x6D:
        return "instruction code not supported or invalid";
    case 0x6E:
        return "class not supported";
    }
    return NULL;
}

static void at_csim_set_error(struct at_userdata *userdata, const char *stage, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void at_csim_set_error(struct at_userdata *userdata, const char *stage, const char *fmt, ...) {
    va_list ap;
    int n = snprintf(userdata->last_error, sizeof(userdata->last_error), "at_csim %s: ", stage);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(userdata->last_error))
        n = sizeof(userdata->last_error) - 1;
    va_start(ap, fmt);
    vsnprintf(userdata->last_error + n, sizeof(userdata->last_error) - (size_t)n, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", userdata->last_error);
}

static void at_csim_set_error_sw(struct at_userdata *userdata, const char *stage, const uint8_t *resp,
                                 const uint32_t resp_len) {
    if (resp_len >= 2) {
        const uint8_t sw1 = resp[resp_len - 2];
        const uint8_t sw2 = resp[resp_len - 1];
        const char *name = at_csim_sw_name(sw1, sw2);
        if (name != NULL)
            at_csim_set_error(userdata, stage, "SW=%02X%02X (%s)", sw1, sw2, name);
        else
            at_csim_set_error(userdata, stage, "SW=%02X%02X", sw1, sw2);
    } else {
        at_csim_set_error(userdata, stage, "short response (%u bytes)", resp_len);
    }
}

static int apdu_interface_connect(struct euicc_ctx *ctx) {
    struct at_userdata *userdata = ctx->apdu.interface->userdata;

    const char *device = getenv_or_default(ENV_AT_DEVICE, userdata->default_device);
    if (at_device_open(userdata, device) != 0) {
        at_csim_set_error(userdata, "connect", "cannot open device %s", device);
        return -1;
    }

    at_emit_command(userdata, "AT");
    if (at_expect(userdata, NULL, NULL) != 0) {
        at_csim_set_error(userdata, "connect", "device not responding to AT");
        return -1;
    }

    at_emit_command(userdata, "AT+CSIM=?");
    if (at_expect(userdata, NULL, NULL) != 0) {
        at_csim_set_error(userdata, "connect", "modem reports no +CSIM support");
        return -1;
    }

    return true;
}

static void apdu_interface_logic_channel_close(struct euicc_ctx *ctx, const uint8_t channel);

static void apdu_interface_disconnect(struct euicc_ctx *ctx) {
    struct at_userdata *userdata = ctx->apdu.interface->userdata;

    if (userdata->last_logic_channel >= 0) {
        fprintf(stderr, "Cleaning up leaked APDU channel %d\n", userdata->last_logic_channel);
        apdu_interface_logic_channel_close(ctx, userdata->last_logic_channel);
    }

    at_device_close(userdata);
}

static int apdu_interface_transmit(struct euicc_ctx *ctx, uint8_t **rx, uint32_t *rx_len, const uint8_t *tx,
                                   const uint32_t tx_len) {
    struct at_userdata *userdata = ctx->apdu.interface->userdata;

    int fret = 0;
    char *response = NULL;

    char *encoded = NULL;

    *rx = NULL;
    *rx_len = 0;

    encoded = malloc(tx_len * 2 + 1);
    euicc_hexutil_bin2hex(encoded, tx_len * 2 + 1, tx, tx_len);
    at_apply_hex_case(encoded);

    at_emit_command(userdata, "AT+CSIM=%u,\"%s\"", tx_len * 2, encoded);
    if (at_expect(userdata, &response, "+CSIM: ") != 0 || response == NULL)
        goto err;

    // Parse the response
    {
        strtok(response, ","); // Skip the length part
        char *payload = strtok(NULL, ","); // Get the response part
        if (payload == NULL) {
            at_csim_set_error(userdata, "transmit", "malformed +CSIM response: no hex field");
            goto err;
        }
        response = payload;
        // unquote strings
        const size_t n = strlen(response);
        if (n >= 2 && response[0] == '"' && response[n - 1] == '"') {
            memmove(response, response + 1, n - 2);
            response[n - 2] = '\0'; // Null-terminate the new string
        }
    }

    *rx_len = strlen(response) / 2;
    *rx = malloc(*rx_len);
    if (*rx == NULL)
        goto err;
    int ret = euicc_hexutil_hex2bin_r(*rx, *rx_len, response, strlen(response));
    if (ret < 0)
        goto err;
    *rx_len = ret;

    goto exit;

err:
    fret = -1;
    free(*rx);
    *rx = NULL;
    *rx_len = 0;
exit:
    free(encoded);
    return fret;
}

static int apdu_interface_logic_channel_open(struct euicc_ctx *ctx, const uint8_t *aid, const uint8_t aid_len) {
    struct at_userdata *userdata = ctx->apdu.interface->userdata;

    _cleanup_free_ char *aid_hex = malloc(aid_len * 2 + 1);
    euicc_hexutil_bin2hex(aid_hex, aid_len * 2 + 1, aid, aid_len);

    // Send MANAGE CHANNEL open
    const uint8_t manage_open[] = {0x00, 0x70, 0x00, 0x00, 0x01};
    _cleanup_free_ uint8_t *resp = NULL;
    uint32_t resp_len = 0;
    if (apdu_interface_transmit(ctx, &resp, &resp_len, manage_open, sizeof(manage_open)) != 0) {
        at_csim_set_error(userdata, "logic_channel_open",
                          "MANAGE CHANNEL open: no +CSIM response (AT-level failure; on modems that "
                          "reject lowercase hex check LPAC_APDU_AT_HEX)");
        return -1;
    }
    if (resp_len != 3 || resp[1] != 0x90 || resp[2] != 0x00) {
        at_csim_set_error_sw(userdata, "logic_channel_open: MANAGE CHANNEL open", resp, resp_len);
        return -1;
    }
    uint8_t channel_byte = resp[0];
    userdata->last_logic_channel = channel_byte;

    // Select AID
    _cleanup_free_ uint8_t *select_cmd = malloc(5 + aid_len);
    if (select_cmd == NULL) {
        at_csim_set_error(userdata, "logic_channel_open", "out of memory");
        apdu_interface_logic_channel_close(ctx, channel_byte);
        return -1;
    }
    select_cmd[0] = channel_byte;
    select_cmd[1] = 0xA4;
    select_cmd[2] = 0x04;
    select_cmd[3] = 0x00;
    select_cmd[4] = aid_len;
    memcpy(select_cmd + 5, aid, aid_len);

    resp = NULL;
    resp_len = 0;
    if (apdu_interface_transmit(ctx, &resp, &resp_len, select_cmd, 5 + aid_len) != 0) {
        at_csim_set_error(userdata, "logic_channel_open", "SELECT by AID: no +CSIM response (AT-level failure)");
        apdu_interface_logic_channel_close(ctx, channel_byte);
        return -1;
    }

    if (resp_len < 2 || (resp[resp_len - 2] != 0x90 && resp[resp_len - 2] != 0x61)) {
        at_csim_set_error_sw(userdata, "logic_channel_open: SELECT by AID", resp, resp_len);
        apdu_interface_logic_channel_close(ctx, channel_byte);
        return -1;
    }
    return (int)channel_byte; // channel number
}

static void apdu_interface_logic_channel_close(struct euicc_ctx *ctx, const uint8_t channel) {
    struct at_userdata *userdata = ctx->apdu.interface->userdata;
    const uint8_t manage_close[] = {0x00, 0x70, 0x80, channel, 0x00};
    _cleanup_free_ uint8_t *resp = NULL;
    uint32_t resp_len = 0;
    apdu_interface_transmit(ctx, &resp, &resp_len, manage_close, sizeof(manage_close));
    if (userdata->last_logic_channel == channel) {
        userdata->last_logic_channel = -1;
    }
}

static int libapduinterface_init(struct euicc_apdu_interface *ifstruct) {
    at_warning_message();

    set_deprecated_env_name(ENV_AT_DEBUG, "AT_DEBUG");
    set_deprecated_env_name(ENV_AT_DEVICE, "AT_DEVICE");

    memset(ifstruct, 0, sizeof(struct euicc_apdu_interface));

    ifstruct->connect = apdu_interface_connect;
    ifstruct->disconnect = apdu_interface_disconnect;
    ifstruct->logic_channel_open = apdu_interface_logic_channel_open;
    ifstruct->logic_channel_close = apdu_interface_logic_channel_close;
    ifstruct->transmit = apdu_interface_transmit;
    ifstruct->userdata = NULL;

    return at_setup_userdata((struct at_userdata **)&ifstruct->userdata);
}

static int libapduinterface_main(const struct euicc_apdu_interface *ifstruct, const int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <list>\n", argv[0]);
        return -1;
    }

    if (strcmp(argv[1], "list") == 0) {
        cJSON *devices = cJSON_CreateArray();
        enumerate_serial_device(devices);
        cJSON *payload = cJSON_CreateObject();
        cJSON_AddStringOrNullToObject(payload, "env", ENV_AT_DEVICE);
        cJSON_AddItemToObject(payload, "data", devices);
        json_print("driver", payload);
    }

    return 0;
}

static void libapduinterface_fini(struct euicc_apdu_interface *ifstruct) {
    struct at_userdata *userdata = ifstruct->userdata;

    at_cleanup_userdata(&userdata);
}

DRIVER_INTERFACE = {
    .type = DRIVER_APDU,
    .name = "at_csim",
    .init = (int (*)(void *))libapduinterface_init,
    .main = (int (*)(void *, int, char **))libapduinterface_main,
    .fini = (void (*)(void *))libapduinterface_fini,
};
