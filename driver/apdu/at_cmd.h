#pragma once

#include <cjson/cJSON.h>
#include <lpac/utils.h>

#include <inttypes.h>

#define AT_BUFFER_SIZE 20480

#define AT_MAX_LOGICAL_CHANNELS 20

#define AT_DEFAULT_DEADLINE_MS 10000
#define AT_RECOVERY_DEADLINE_MS 30000
#define AT_CGLA_DEADLINE_MS 240000
#define AT_PROBE_DEADLINE_MS 3000
#define AT_POLL_SLICE_MS 500

#define ENV_AT_DEBUG APDU_ENV_NAME(AT, DEBUG)
#define ENV_AT_DEVICE APDU_ENV_NAME(AT, DEVICE)
#define ENV_AT_HEX APDU_ENV_NAME(AT, HEX)
#define ENV_AT_TIMEOUT_MS APDU_ENV_NAME(AT, TIMEOUT_MS)

/**
 * Runtime deadline override (#856 BPP-install timeout):
 *
 * The default 10s per-APDU deadline is too short for STORE DATA
 * sequences on a card with accumulated NVM writes — the BPP install
 * (es10b_load_bound_profile_package) sends ~300 APDUs and any single
 * one exceeding the deadline aborts the whole download.
 *
 * LPAC_APDU_AT_TIMEOUT_MS=<ms> raises the per-APDU deadline for ALL
 * at_csim operations (reads included — they just complete faster than
 * the deadline). Use 30000-60000 for BPP installs on tired cards.
 * Default (unset) keeps the 10s compile-time constant.
 *
 * CGLA keeps its own 240s deadline (a different operation class) and
 * is NOT affected by this override.
 */
static inline int at_effective_deadline_ms(void) {
    const char *env = getenv(ENV_AT_TIMEOUT_MS);
    if (env && *env) {
        int ms = atoi(env);
        if (ms >= 1000)  /* floor: 1s; refuse absurd values */
            return ms;
    }
    return AT_DEFAULT_DEADLINE_MS;
}

struct at_userdata {
    char *default_device;
#if defined(_WIN32)
    HANDLE hComm;
#else
    int fd;
#endif

    char *at_cmd_buffer;
    // A persistent buffer for reading data from the serial port
    char at_read_buffer[AT_BUFFER_SIZE];
    size_t at_read_buffer_len;

    char **channels;

    // Last failure description set by the at_csim driver (empty = none);
    // echoed to stderr at the failing stage so `euicc_init` failures are
    // diagnosable without a wire capture.
    char last_error[192];

    struct timespec wall;
};

#define AT_DEBUG(direct, line)                                                                       \
    do {                                                                                             \
        if (getenv_or_default(ENV_AT_DEBUG, (bool)false)) {                                          \
            struct timespec t = get_wall_time(userdata->wall);                                       \
            fprintf(stderr, "AT_DEBUG_" direct "(%" PRId64 ".%" PRId64 "): %s\n", (int64_t)t.tv_sec, \
                    (int64_t)t.tv_nsec, line);                                                       \
        }                                                                                            \
    } while (0)
#define AT_DEBUG_TX(line) AT_DEBUG("TX", line)
#define AT_DEBUG_RX(line) AT_DEBUG("RX", line)

int enumerate_serial_device(cJSON *device);

int at_write_command(struct at_userdata *userdata, const char *command);

int at_expect_with_deadline(struct at_userdata *userdata, char **response, const char *expected, int deadline_ms);

int at_expect(struct at_userdata *userdata, char **response, const char *expected);

int at_device_open(struct at_userdata *userdata, const char *device_name);

int at_device_close(struct at_userdata *userdata);

int at_setup_userdata(struct at_userdata **userdata);

void at_cleanup_userdata(struct at_userdata **userdata);
