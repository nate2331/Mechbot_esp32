#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <expansion/expansion.h>
#include <power/power_service/power.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include "RvcParser.h"

// DRAFT CHECKPOINT: not compiled, installed, or hardware validated.
// See DEVELOPMENT_STATUS.md before resuming this app.
// STRDC R1 only: TX is SDA/MISO/TX. P0 must be strapped to VDC, P1 low.
// No TX wire, reset wire, ESP32 wire, or external power supply is used.
// The app cannot prevent a user reboot, forced termination or external rewiring.
namespace {
constexpr uint32_t RunMs = 35000;
constexpr uint32_t SettleMs = 2500;
constexpr uint32_t PollMs = 250;
constexpr const char* LogDir = "/ext/apps_data/imu_rvc_test";
enum class Phase { Disconnected, Ready, Running, PowerDown, Result, Disconnect };

struct App {
    Gui* gui = nullptr;
    Storage* storage = nullptr;
    Power* power = nullptr;
    Expansion* expansion = nullptr;
    ViewPort* viewport = nullptr;
    FuriMessageQueue* keys = nullptr;
    FuriMutex* display_mutex = nullptr;
    FuriStreamBuffer* stream = nullptr;
    FuriHalSerialHandle* serial = nullptr;
    File* raw = nullptr;
    File* report = nullptr;
    bool expansion_disabled = false;
    bool serial_initialized = false;
    bool rx_started = false;
    bool logs_open = false;
    bool log_error = false;
    bool final_written = false;
    bool exit_requested = false;
    bool have_low = false;
    bool safe_low = false;
    uint32_t low_since = 0;
    uint32_t last_power_poll = 0;
    uint32_t start_tick = 0;
    uint32_t stopped_ms = 0;
    uint32_t last_status_ms = 0;
    float vbus = -1.0f;
    float run_vbus_min = 100.0f;
    float run_vbus_max = 0.0f;
    Phase phase = Phase::Disconnected;
    char lines[6][36] = {};
    char reason[48] = "Not started";
    char basename[40] = {};

    // Single ISR writer; aligned 32-bit reads are atomic on this MCU.
    volatile bool accepting = false;
    volatile uint32_t bytes_irq = 0;
    volatile uint32_t dropped = 0;
    volatile uint32_t frame_errors = 0;
    volatile uint32_t noise_errors = 0;
    volatile uint32_t overrun_errors = 0;
    volatile uint32_t parity_errors = 0;
    volatile uint32_t first_byte_tick = 0;
    volatile uint32_t last_byte_tick = 0;
    RvcParser parser;
    uint32_t parsed_bytes = 0;
    uint32_t last_frame_ms = 0;
    uint32_t repeated_indices = 0;
    uint32_t fresh_indices = 0;
    uint32_t angle_changes = 0;
    uint32_t accel_changes = 0;
    bool have_previous = false;
    RvcParser::Sample previous;
    uint8_t prefix[128] = {};
    size_t prefix_size = 0;
};

uint32_t elapsed(uint32_t then) {
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(furi_get_tick() - then) * 1000U) /
        furi_kernel_get_tick_frequency());
}

uint32_t since_start(const App* app, uint32_t tick) {
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(tick - app->start_tick) * 1000U) /
        furi_kernel_get_tick_frequency());
}

void logf(App* app, const char* format, ...) {
    if(!app->logs_open || app->log_error) return;
    char buffer[512];
    va_list args;
    va_start(args, format);
    const int result = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if(result < 0) {
        app->log_error = true;
        return;
    }
    const size_t length = static_cast<size_t>(result) < sizeof(buffer) ?
                              static_cast<size_t>(result) : sizeof(buffer) - 1;
    if(storage_file_write(app->report, buffer, length) != length) app->log_error = true;
}

void draw(Canvas* canvas, void* context) {
    auto* app = static_cast<App*>(context);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    furi_mutex_acquire(app->display_mutex, FuriWaitForever);
    for(size_t i = 0; i < 6; ++i) canvas_draw_str(canvas, 0, 10 + i * 10, app->lines[i]);
    furi_mutex_release(app->display_mutex);
}

void key(InputEvent* event, void* context) {
    auto* app = static_cast<App*>(context);
    if(event->type == InputTypeShort)
        furi_message_queue_put(app->keys, event, 0);
}

void rx(FuriHalSerialHandle* serial, FuriHalSerialRxEvent event, void* context) {
    auto* app = static_cast<App*>(context);
    if(event & FuriHalSerialRxEventData) {
        const uint8_t byte = furi_hal_serial_async_rx(serial);
        if(app->accepting) {
            const uint32_t tick = furi_get_tick();
            if(app->bytes_irq == 0) app->first_byte_tick = tick;
            app->last_byte_tick = tick;
            ++app->bytes_irq;
            if(furi_stream_buffer_send(app->stream, &byte, 1, 0) != 1) ++app->dropped;
        }
    }
    if(app->accepting) {
        if(event & FuriHalSerialRxEventFrameError) ++app->frame_errors;
        if(event & FuriHalSerialRxEventNoiseError) ++app->noise_errors;
        if(event & FuriHalSerialRxEventOverrunError) ++app->overrun_errors;
        if(event & FuriHalSerialRxEventParityError) ++app->parity_errors;
    }
}

bool rx_safe(const App* app) {
    if(!app->serial_initialized) return false;
    const GpioPin* pin = furi_hal_serial_get_gpio_pin(app->serial, FuriHalSerialDirectionRx);
    if(pin->port != GPIOB || pin->pin != GPIO_PIN_7) return false;
    // PB7: alternate function mode, no pulls, USART1 AF7. Read actual registers.
    return ((GPIOB->MODER >> 14) & 3U) == 2U &&
           ((GPIOB->PUPDR >> 14) & 3U) == 0U &&
           ((GPIOB->AFR[0] >> 28) & 15U) == 7U &&
           (PWR->PUCRB & GPIO_PIN_7) == 0U && (PWR->PDCRB & GPIO_PIN_7) == 0U;
}

void force_power_off(App* app) {
    // Immediate hardware stop, then clear the service's remembered request.
    furi_hal_power_disable_otg();
    power_enable_otg(app->power, false);
    app->safe_low = false;
    app->have_low = false;
}

void poll_power(App* app) {
    app->vbus = furi_hal_power_get_usb_voltage();
    // BQ25896 reports zero when VBUS_GD is clear; this is NOT a precision
    // discharge measurement. Stable absence supplements, never replaces,
    // the required physical IMU-disconnection confirmation before release.
    const bool low = app->vbus >= 0.0f && app->vbus < 0.5f &&
                     !furi_hal_power_is_otg_enabled() && !power_is_otg_enabled(app->power);
    if(low) {
        if(!app->have_low) {
            app->have_low = true;
            app->low_since = furi_get_tick();
        }
        app->safe_low = elapsed(app->low_since) >= SettleMs;
    } else {
        app->have_low = false;
        app->safe_low = false;
    }
    if(app->phase == Phase::Running) {
        if(app->vbus < app->run_vbus_min) app->run_vbus_min = app->vbus;
        if(app->vbus > app->run_vbus_max) app->run_vbus_max = app->vbus;
    }
    app->last_power_poll = furi_get_tick();
}

bool prepare_receiver(App* app) {
    if(!app->safe_low) return false;
    expansion_disable(app->expansion);
    app->expansion_disabled = true;
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app->serial) {
        snprintf(app->reason, sizeof(app->reason), "UART busy; unplug IMU");
        return false;
    }
    furi_hal_serial_init(app->serial, 115200);
    app->serial_initialized = true;
    furi_hal_serial_configure_framing(
        app->serial, FuriHalSerialDataBits8, FuriHalSerialParityNone,
        FuriHalSerialStopBits1);
    furi_hal_serial_disable_direction(app->serial, FuriHalSerialDirectionTx);
    furi_hal_gpio_init_ex(
        &gpio_usart_rx, GpioModeAltFunctionPushPull, GpioPullNo,
        GpioSpeedVeryHigh, GpioAltFn7USART1);
    if(!rx_safe(app)) {
        snprintf(app->reason, sizeof(app->reason), "RX pin check failed");
        return false;
    }
    furi_hal_serial_async_rx_start(app->serial, rx, app, true);
    app->rx_started = true;
    return rx_safe(app);
}

bool open_logs(App* app) {
    storage_common_mkdir(app->storage, "/ext/apps_data");
    storage_common_mkdir(app->storage, LogDir);
    app->raw = storage_file_alloc(app->storage);
    app->report = storage_file_alloc(app->storage);
    const uint32_t stamp = furi_hal_rtc_get_timestamp();
    char path[128];
    bool unique = false;
    for(unsigned i = 0; i < 1000; ++i) {
        snprintf(app->basename, sizeof(app->basename), "rvc_%lu_%03u", (unsigned long)stamp, i);
        snprintf(path, sizeof(path), "%s/%s.bin", LogDir, app->basename);
        if(storage_common_exists(app->storage, path)) continue;
        char text_path[128];
        snprintf(text_path, sizeof(text_path), "%s/%s.txt", LogDir, app->basename);
        if(storage_common_exists(app->storage, text_path)) continue;
        if(!storage_file_open(app->raw, path, FSAM_WRITE, FSOM_CREATE_NEW)) break;
        if(!storage_file_open(app->report, text_path, FSAM_WRITE, FSOM_CREATE_NEW)) break;
        unique = true;
        break;
    }
    if(!unique) {
        // Storage requires close even after failed open. Preserve partial files.
        storage_file_close(app->raw);
        storage_file_close(app->report);
        storage_file_free(app->raw);
        storage_file_free(app->report);
        app->raw = app->report = nullptr;
        return false;
    }
    app->logs_open = true;
    return true;
}

void drain(App* app) {
    uint8_t buffer[512];
    size_t count;
    // Bounded per main-loop visit; prevents a noisy full-rate UART starving Stop.
    unsigned chunks = 0;
    while(chunks++ < 8 &&
          (count = furi_stream_buffer_receive(app->stream, buffer, sizeof(buffer), 0)) != 0) {
        if(app->logs_open && !app->log_error &&
           storage_file_write(app->raw, buffer, count) != count) app->log_error = true;
        for(size_t i = 0; i < count; ++i) {
            ++app->parsed_bytes;
            if(app->prefix_size < sizeof(app->prefix)) app->prefix[app->prefix_size++] = buffer[i];
            if(app->parser.feed(buffer[i])) {
                const auto& sample = app->parser.sample();
                if(app->have_previous) {
                    if(sample.index == app->previous.index) ++app->repeated_indices;
                    else ++app->fresh_indices;
                    if(sample.yaw != app->previous.yaw || sample.pitch != app->previous.pitch ||
                       sample.roll != app->previous.roll) ++app->angle_changes;
                    if(sample.ax != app->previous.ax || sample.ay != app->previous.ay ||
                       sample.az != app->previous.az) ++app->accel_changes;
                }
                app->previous = sample;
                app->have_previous = true;
                app->last_frame_ms = elapsed(app->start_tick);
            }
        }
    }
}

void status(App* app, const char* label) {
    const auto& s = app->parser.sample();
    const uint32_t now_ms = app->phase == Phase::Running ? elapsed(app->start_tick) : app->stopped_ms;
    logf(app, "%s ms=%lu bytes_irq=%lu parsed=%lu valid=%lu bad_checksum=%lu "
              "index_fresh=%lu index_repeat=%lu index_discontinuity=%lu "
              "angle_changes=%lu accel_changes=%lu last_frame_ms=%ld "
              "xyz_angles_cdeg=%d,%d,%d accel_mg=%d,%d,%d vbus=%.2f\n",
         label, (unsigned long)now_ms, (unsigned long)app->bytes_irq,
         (unsigned long)app->parsed_bytes, (unsigned long)app->parser.validFrames(),
         (unsigned long)app->parser.badChecksums(), (unsigned long)app->fresh_indices,
         (unsigned long)app->repeated_indices, (unsigned long)app->parser.indexDiscontinuities(),
         (unsigned long)app->angle_changes, (unsigned long)app->accel_changes,
         app->have_previous ? (long)app->last_frame_ms : -1L,
         s.yaw, s.pitch, s.roll, s.ax, s.ay, s.az, (double)app->vbus);
}

void stop(App* app, const char* reason) {
    if(app->phase == Phase::Running) app->stopped_ms = elapsed(app->start_tick);
    app->accepting = false;
    force_power_off(app);
    snprintf(app->reason, sizeof(app->reason), "%s", reason);
    app->phase = Phase::PowerDown;
}

void finish_logs(App* app) {
    if(!app->logs_open || app->final_written) return;
    status(app, "FINAL");
    logf(app, "stop=%s ring_dropped=%lu framing=%lu noise=%lu overrun=%lu parity=%lu "
              "first_byte_ms=%ld last_byte_ms=%ld run_vbus_min=%.2f run_vbus_max=%.2f "
              "off_vbus=%.2f RX_AF7_NoPull=%u\n",
         app->reason, (unsigned long)app->dropped, (unsigned long)app->frame_errors,
         (unsigned long)app->noise_errors, (unsigned long)app->overrun_errors,
         (unsigned long)app->parity_errors,
         app->bytes_irq ? (long)since_start(app, app->first_byte_tick) : -1L,
         app->bytes_irq ? (long)since_start(app, app->last_byte_tick) : -1L,
         (double)app->run_vbus_min, (double)app->run_vbus_max, (double)app->vbus, rx_safe(app));
    logf(app, "PREFIX_HEX ");
    for(size_t i = 0; i < app->prefix_size; ++i) logf(app, "%02X ", app->prefix[i]);
    logf(app, "\nPREFIX_ASCII ");
    for(size_t i = 0; i < app->prefix_size; ++i) {
        const uint8_t c = app->prefix[i];
        logf(app, "%c", c >= 32 && c <= 126 ? c : '.');
    }
    logf(app, "\nRaw .bin is byte-preserving unless ring/UART/storage errors are reported.\n"
              "Repeated physical values may be normal. Index discontinuities are not a loss count.\n"
              "No RVC frames does not establish physical damage. No gyro-rate report is in RVC.\n"
              "VBUS is a coarse charger indication, not proof of electrical disconnection.\n");
    if(!storage_file_sync(app->raw) || !storage_file_sync(app->report)) app->log_error = true;
    if(!storage_file_close(app->raw)) app->log_error = true;
    if(!storage_file_close(app->report)) app->log_error = true;
    storage_file_free(app->raw);
    storage_file_free(app->report);
    app->raw = app->report = nullptr;
    app->logs_open = false;
    app->final_written = true;
}

bool start(App* app) {
    // One trial per launch avoids accidental repeat power cycles and keeps results visible.
    poll_power(app);
    if(!app->safe_low || !rx_safe(app)) return false;
    if(!open_logs(app)) {
        snprintf(app->reason, sizeof(app->reason), "SD log open failed");
        app->phase = Phase::Result;
        app->log_error = true;
        return false;
    }
    logf(app, "IMU_RVC_TEST v0.1 custom Flipper FAP; 35 seconds; RX PB7 pin14; "
              "115200 8N1; AF7 NoPull; UART TX direction disabled.\n"
              "STRDC BNO085: VDC pin1 5V, GND pin8, SDA/MISO/TX pin14, P0 to VDC.\n"
              "P1 default LOW; BT default HIGH; no ESP32 or other IMU wires.\n"
              "Power service request held OFF; single HAL boost attempt; no auto retries.\n");
    if(!storage_file_sync(app->report)) app->log_error = true;
    poll_power(app); // File creation may have taken time; check again before power.
    if(app->log_error || !app->safe_low || !rx_safe(app)) {
        stop(app, "Start check failed");
        return false;
    }
    furi_stream_buffer_reset(app->stream);
    app->start_tick = furi_get_tick();
    app->phase = Phase::Running;
    app->safe_low = app->have_low = false;
    app->accepting = true; // Receiver and counters precede IMU power application.
    const bool enabled = furi_hal_power_enable_otg(); // Deliberately ONE attempt.
    logf(app, "BOOST enable=%u actual_enabled=%u at_ms=%lu RX_AF7_NoPull=%u\n",
         enabled, furi_hal_power_is_otg_enabled(),
         (unsigned long)elapsed(app->start_tick), rx_safe(app));
    if(!enabled || !furi_hal_power_is_otg_enabled()) {
        stop(app, "Boost enable failed");
        return false;
    }
    return true;
}

void screen(App* app) {
    furi_mutex_acquire(app->display_mutex, FuriWaitForever);
    memset(app->lines, 0, sizeof(app->lines));
    auto line = [app](unsigned n, const char* text) {
        snprintf(app->lines[n], sizeof(app->lines[n]), "%s", text);
    };
    switch(app->phase) {
    case Phase::Disconnected:
        line(0, "IMU RVC - battery only");
        line(1, "Keep IMU DISCONNECTED");
        line(2, "Unplug Flipper USB");
        snprintf(app->lines[3], sizeof(app->lines[3]), "VBUS report: %.2f V", (double)app->vbus);
        line(4, app->safe_low ? "OK: prepare RX safely" : "Waiting for power off...");
        line(5, "Back: exit (IMU unplugged)");
        break;
    case Phase::Ready:
        line(0, "READY: RX no pull, 5V OFF");
        line(1, "Now connect IMU only:");
        line(2, "VDC-1 GND-8 SDA/TX-14");
        line(3, "P0-VDC; leave others free");
        line(4, "Keep USB unplugged!");
        line(5, "OK: START 35s  Back: exit");
        break;
    case Phase::Running:
        snprintf(app->lines[0], sizeof(app->lines[0]), "RVC capture %lu / 35s", (unsigned long)(elapsed(app->start_tick) / 1000));
        snprintf(app->lines[1], sizeof(app->lines[1]), "Bytes %lu  Frames %lu", (unsigned long)app->bytes_irq, (unsigned long)app->parser.validFrames());
        snprintf(app->lines[2], sizeof(app->lines[2]), "Fresh %lu  Repeat %lu", (unsigned long)app->fresh_indices, (unsigned long)app->repeated_indices);
        snprintf(app->lines[3], sizeof(app->lines[3]), "CRC %lu Drop %lu V %.1f", (unsigned long)app->parser.badChecksums(), (unsigned long)app->dropped, (double)app->vbus);
        line(4, "Do NOT connect USB");
        line(5, "Back: stop + power off");
        break;
    case Phase::PowerDown:
        line(0, "Boost OFF; RX retained");
        line(1, "UNPLUG USB / IMU");
        snprintf(app->lines[2], sizeof(app->lines[2]), "VBUS report: %.2f V", (double)app->vbus);
        line(3, "Waiting for rail to fall");
        line(4, "Do not reboot Flipper");
        line(5, "Result follows power off");
        break;
    case Phase::Result:
        line(0, app->log_error ? "RESULT - SD WRITE ERROR" : "RESULT - power off");
        snprintf(app->lines[1], sizeof(app->lines[1]), "Bytes %lu  Frames %lu", (unsigned long)app->bytes_irq, (unsigned long)app->parser.validFrames());
        snprintf(app->lines[2], sizeof(app->lines[2]), "Fresh %lu  Repeat %lu", (unsigned long)app->fresh_indices, (unsigned long)app->repeated_indices);
        snprintf(app->lines[3], sizeof(app->lines[3]), "CRC %lu Drop %lu", (unsigned long)app->parser.badChecksums(), (unsigned long)app->dropped);
        line(4, app->reason);
        line(5, "Back: disconnect + exit");
        break;
    case Phase::Disconnect:
        line(0, "Before leaving this app:");
        line(1, "UNPLUG ALL IMU WIRES");
        line(2, "Keep USB unplugged");
        line(3, "RX kept safe until exit");
        line(4, app->safe_low ? "OK: confirm disconnected" : "Waiting for rail to fall");
        line(5, "Back: return to result");
        break;
    }
    furi_mutex_release(app->display_mutex);
    view_port_update(app->viewport);
}
} // namespace

extern "C" int32_t imu_rvc_test_app(void* context) {
    UNUSED(context);
    auto* app = new App();
    app->keys = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->display_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->stream = furi_stream_buffer_alloc(16384, 1);
    app->gui = static_cast<Gui*>(furi_record_open(RECORD_GUI));
    app->storage = static_cast<Storage*>(furi_record_open(RECORD_STORAGE));
    app->power = static_cast<Power*>(furi_record_open(RECORD_POWER));
    app->expansion = static_cast<Expansion*>(furi_record_open(RECORD_EXPANSION));
    furi_hal_power_insomnia_enter();
    force_power_off(app);
    app->viewport = view_port_alloc();
    view_port_draw_callback_set(app->viewport, draw, app);
    view_port_input_callback_set(app->viewport, key, app);
    gui_add_view_port(app->gui, app->viewport, GuiLayerFullscreen);
    screen(app);

    while(!app->exit_requested) {
        if(elapsed(app->last_power_poll) >= PollMs) {
            poll_power(app);
            if(app->phase == Phase::Running &&
               (!furi_hal_power_is_otg_enabled() || !rx_safe(app)))
                stop(app, "Power or RX check failed");
            if(app->phase == Phase::Ready && !app->safe_low)
                stop(app, "Unexpected external power");
        }
        if(app->phase == Phase::Running) {
            if(elapsed(app->start_tick) >= RunMs) stop(app, "35 second trial complete");
            else if(app->log_error) stop(app, "SD write failed");
        }
        if(app->logs_open) drain(app);
        if(app->phase == Phase::Running && elapsed(app->start_tick) - app->last_status_ms >= 1000) {
            app->last_status_ms = elapsed(app->start_tick);
            status(app, "STATUS");
        }
        if(app->phase == Phase::PowerDown && app->safe_low &&
           furi_stream_buffer_is_empty(app->stream)) {
            finish_logs(app);
            app->phase = Phase::Result;
        }

        InputEvent event;
        if(furi_message_queue_get(app->keys, &event, furi_ms_to_ticks(20)) == FuriStatusOk) {
            if(event.key == InputKeyBack) {
                if(app->phase == Phase::Running) stop(app, "Stopped by user");
                else if(app->phase == Phase::Disconnected) app->exit_requested = true;
                else if(app->phase == Phase::Ready || app->phase == Phase::Result)
                    app->phase = Phase::Disconnect;
                else if(app->phase == Phase::Disconnect) app->phase = Phase::Result;
            } else if(event.key == InputKeyOk) {
                if(app->phase == Phase::Disconnected && app->safe_low) {
                    if(prepare_receiver(app)) app->phase = Phase::Ready;
                    else app->phase = Phase::Result;
                } else if(app->phase == Phase::Ready) start(app);
                else if(app->phase == Phase::Disconnect) {
                    poll_power(app);
                    if(app->safe_low) app->exit_requested = true;
                }
            }
        }
        screen(app);
    }

    // Reached with a user disconnect confirmation for any acquired UART.
    // No timeout bypasses the rail-off gate or restores expansion while live.
    force_power_off(app);
    app->accepting = false;
    if(app->rx_started) furi_hal_serial_async_rx_stop(app->serial);
    if(app->serial_initialized) furi_hal_serial_deinit(app->serial);
    if(app->serial) furi_hal_serial_control_release(app->serial);
    if(app->expansion_disabled) expansion_enable(app->expansion);
    gui_remove_view_port(app->gui, app->viewport);
    view_port_free(app->viewport);
    furi_record_close(RECORD_EXPANSION);
    furi_record_close(RECORD_POWER);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    furi_stream_buffer_free(app->stream);
    furi_message_queue_free(app->keys);
    furi_mutex_free(app->display_mutex);
    furi_hal_power_insomnia_exit();
    delete app;
    return 0;
}
