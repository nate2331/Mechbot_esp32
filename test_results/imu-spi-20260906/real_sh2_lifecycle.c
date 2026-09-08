#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "sh2.h"
#include "sh2_err.h"

static unsigned open_calls, close_calls, read_calls;
static int requested_open_result;
static uint32_t fake_us;
static int allocated_via_open;

static int fake_open(sh2_Hal_t *self) {
    (void)self;
    assert(!allocated_via_open);
    allocated_via_open = 1;
    ++open_calls;
    return requested_open_result;
}
static void fake_close(sh2_Hal_t *self) {
    (void)self;
    assert(allocated_via_open);
    allocated_via_open = 0;
    ++close_calls;
}
static int fake_read(sh2_Hal_t *self, uint8_t *buf, unsigned len, uint32_t *stamp) {
    (void)self; (void)buf; (void)len; (void)stamp;
    ++read_calls;
    return 0;
}
static int fake_write(sh2_Hal_t *self, uint8_t *buf, unsigned len) {
    (void)self; (void)buf;
    return (int)len;
}
static uint32_t fake_time(sh2_Hal_t *self) {
    (void)self;
    fake_us += 1000;
    return fake_us;
}
static sh2_Hal_t hal = {fake_open, fake_close, fake_read, fake_write, fake_time};

int main(void) {
    for (unsigned cycle = 0; cycle < 6; ++cycle) {
        requested_open_result = (cycle % 2) ? 0 : -1;
        const unsigned before = open_calls;
        const int result = sh2_open(&hal, NULL, NULL);
        assert(result == SH2_OK);
        assert(open_calls == before + 1 && allocated_via_open);
        sh2_ProductIds_t ids = {0};
        const int ids_result = sh2_getProdIds(&ids);
        assert(ids_result != SH2_OK);
        printf("cycle=%u HAL.open=%d sh2_open=%d product_ids=%d allocated=%d\n", cycle, requested_open_result, result, ids_result, allocated_via_open);
        if (allocated_via_open) sh2_close();
        assert(!allocated_via_open && close_calls == open_calls);
    }
    puts("PASS: HAL.open error ignored; guarded close releases real SHTP slot after failed initialization; six retries work.");

    requested_open_result = -1;
    assert(sh2_open(&hal, NULL, NULL) == SH2_OK);
    const unsigned occupied_open_count = open_calls;
    const int second_result = sh2_open(&hal, NULL, NULL);
    assert(second_result != SH2_OK);
    assert(open_calls == occupied_open_count && allocated_via_open);
    printf("PASS: unclosed first session makes second sh2_open fail (%d) before HAL.open. No unsafe close attempted after overwritten global state. reads=%u opens=%u closes=%u\n", second_result, read_calls, open_calls, close_calls);
    return 0;
}
