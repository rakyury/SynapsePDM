/* stdin/stdout harness for the ACTUAL protocol and Racing Dash C VM.
 * RX <now_decimal> <id_hex> <16hexbytes> [dlc extended remote]
 * TICK <now_decimal>, SET <channel_decimal> <value_decimal>, RESET
 * Every command prints zero or more '<idhex> <16hexbytes>' and then END.
 * SET permits only diagnostic channels240/241/242/244, never output gates.
 */
#include "../src/pdm_link.h"
#include "../src/pdm_adc.h"
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static PDM_Link link;
static ChannelEngine_t channels;
static VM_t vm;
static PDM_ADC adc;
/* Explicit simulator fixtures, never device calibration defaults. */
static PDM_ADCCalibration calibration = {1500, 1000, 1300};
static uint32_t received;
static void reset(void)
{
    Channel_Init(&channels); VM_Init(&vm, &channels); PDM_Link_Init(&link, &channels, &vm);
    PDM_ADC_Init(&adc, calibration);
    PDM_Link_SetADCReader(&link, PDM_ADC_Read, &adc);
    received = 0;
}
static bool words(const char *line, uint32_t *values, unsigned count)
{
    while (*line && !isspace((unsigned char)*line)) ++line;
    for (unsigned i = 0; i < count; ++i) {
        while (isspace((unsigned char)*line)) ++line;
        if (!isdigit((unsigned char)*line)) return false;
        char *end;
        errno = 0;
        unsigned long value = strtoul(line, &end, 10);
        if (errno || value > UINT32_MAX || (*end && !isspace((unsigned char)*end))) return false;
        values[i] = (uint32_t)value; line = end;
    }
    while (isspace((unsigned char)*line)) ++line;
    return *line == '\0';
}
static int nibble(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
int main(void)
{
    char line[256], command[16];
    reset();
    while (fgets(line, sizeof(line), stdin) != NULL) {
        bool valid = false;
        if (sscanf(line, "%15s", command) != 1) return 2;
        if (strcmp(command, "RESET") == 0) { reset(); valid = true; }
        else if (strcmp(command, "CAL") == 0) {
            uint32_t v[3];
            if (words(line, v, 3) && v[0] <= UINT16_MAX && v[1] <= UINT16_MAX && v[2] <= UINT16_MAX) {
                calibration = (PDM_ADCCalibration){(uint16_t)v[0], (uint16_t)v[1], (uint16_t)v[2]};
                PDM_ADC_Init(&adc, calibration); valid = true;
            }
        } else if (strcmp(command, "ADC") == 0) {
            uint32_t v[1 + PDM_ADC_RAW_COUNT];
            if (words(line, v, 1 + PDM_ADC_RAW_COUNT)) {
                uint16_t raw[PDM_ADC_RAW_COUNT]; valid = true;
                for (unsigned i = 0; i < PDM_ADC_RAW_COUNT; ++i) {
                    if (v[i + 1] > UINT16_MAX) { valid = false; break; }
                    raw[i] = (uint16_t)v[i + 1];
                }
                if (valid) PDM_ADC_SubmitRaw(&adc, raw, v[0]);
            }
        } else if (strcmp(command, "ADCFAIL") == 0) {
            uint32_t v[1];
            if (words(line, v, 1) && (v[0] == 1 || v[0] == 3 || v[0] == 4)) {
                PDM_ADC_Invalidate(&adc, (uint8_t)v[0]); valid = true;
            }
        }
        else if (strcmp(command, "TICK") == 0) {
            uint32_t now;
            if (sscanf(line, "%*s %" SCNu32, &now) == 1) { PDM_Link_Tick(&link, now); valid = true; }
        } else if (strcmp(command, "SET") == 0) {
            unsigned channel; int32_t value;
            if (sscanf(line, "%*s %u %" SCNd32, &channel, &value) == 2 &&
                (channel == 240 || channel == 241 || channel == 242 || channel == 244)) {
                Channel_SetValue(&channels, (uint16_t)channel, value); valid = true;
            }
        } else if (strcmp(command, "RX") == 0) {
            uint32_t now; unsigned id, dlc = 8, extended = 0, remote = 0; char hex[18];
            int fields = sscanf(line, "%*s %" SCNu32 " %x %17s %u %u %u", &now, &id, hex, &dlc, &extended, &remote);
            if ((fields == 3 || fields == 6) && strlen(hex) == 16 && dlc <= 255) {
                PDM_LinkFrame frame = {.id = id, .dlc = (uint8_t)dlc, .extended = extended != 0, .remote = remote != 0};
                valid = true;
                for (unsigned i = 0; i < 8; ++i) {
                    int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
                    if (hi < 0 || lo < 0) { valid = false; break; }
                    frame.data[i] = (uint8_t)(hi * 16 + lo);
                }
                if (valid) {
                    if (id <= 0x7ff && dlc <= 8 && !frame.extended && !frame.remote) {
                        Channel_SetValue(&channels, 240, (int32_t)(++received & 0x7fffffffu));
                        Channel_SetValue(&channels, 241, (int32_t)id);
                    }
                    PDM_Link_Receive(&link, &frame, now);
                }
            }
        }
        if (!valid) puts("ERROR");
        const PDM_LinkFrame *frame;
        while ((frame = PDM_Link_PeekTx(&link)) != NULL) {
            printf("%03" PRIx32 " ", frame->id);
            for (unsigned i = 0; i < 8; ++i) printf("%02x", frame->data[i]);
            putchar('\n'); PDM_Link_PopTx(&link);
        }
        puts("END"); fflush(stdout);
    }
    return 0;
}
