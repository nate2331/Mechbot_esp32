/*
  Maker ESP32 Pro / SS6625E frequency experiment. Arduino ESP32 core 3.x.
  Serial 115200, newline. Boot stopped. Only ONE selected wheel is powered.
  Default: FL / M2, 175/256 duty, 8-bit AUTO, 8-second command.
  "run 248 f", "sweep", "fine"; x or ! stops immediately without newline.
  Read README.md before running. No external libraries, PID, ramps or radio.
*/
#include <Arduino.h>
#include <esp_arduino_version.h>
#include <soc/soc.h>
#include <soc/gpio_reg.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if !defined(CONFIG_IDF_TARGET_ESP32) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "Use ESP32 Dev Module and Espressif Arduino ESP32 core 3.x."
#endif

// Matches the earlier bare 248 Hz sketch, including physical wheel polarity.
struct Wheel {
  const char *name;
  uint8_t in1, in2, encA, encB;
  int8_t motorSign, encoderSign;
  double countsPerTurn;
};
DRAM_ATTR const Wheel wheels[4] = {
  {"fl", 17,12,35,36, 1, 1,2468.8}, // M2 / E2
  {"fr", 14,15,34,39, 1,-1,2467.9}, // M3 / E3
  {"rl",  4, 2, 5,23, 1, 1,2473.5}, // M1 / E1
  {"rr", 27,13,18,19,-1,-1,2469.8}  // M0 / E0
};
constexpr uint32_t RUN_US = 8000000;
constexpr uint32_t SAMPLE_US = 100000;
constexpr uint32_t REST_US = 3000000;
constexpr uint32_t QUIET_US = 500000;
constexpr uint32_t REST_TIMEOUT_US = 15000000;
// A disconnected encoder and a stationary motor cannot be distinguished here.
// Abort the whole sequence if no valid edge occurs for this long while powered.
constexpr uint32_t NO_EDGE_US = 1500000;
constexpr uint16_t MAX_SAMPLES = RUN_US / SAMPLE_US + 3;
constexpr uint16_t MAX_TRIALS = 160;

enum class Phase : uint8_t { Idle, Rest, Running };
enum class Profile : uint8_t { History, Apb, Ref };
struct Trial { uint32_t hz; int8_t direction; uint8_t block; };
struct Reading { uint32_t us; int64_t ticks; uint32_t edges, invalid; };
struct Sample { uint32_t us; int64_t ticks; uint32_t edges, invalid; };

Phase phase = Phase::Idle;
Profile profile = Profile::History;
uint8_t wheelIndex = 0, blocks = 3;
uint16_t duty256 = 175;
uint32_t seed = 2482026, randomState = 1;
bool channelReady[8] = {};
bool pwmInitialized = false;
bool hardwareFault = false;
Trial plan[MAX_TRIALS], trial = {};
uint16_t planCount = 0, planPosition = 0;
uint32_t trialNumber = 0, sequenceNumber = 0, frequencyReadback = 0;
uint32_t commandUs = 0, lastStopUs = 0, restBeginUs = 0, quietSinceUs = 0;
uint32_t restEdges = 0, restInvalid = 0, firstEdgeUs = 0, lastEdgeUs = 0;
uint32_t observedEdges = 0;
uint32_t restElapsedUs = 0, nextSampleUs = SAMPLE_US;
Reading baseline = {};
Sample samples[MAX_SAMPLES];
uint16_t sampleCount = 0;
char input[96];
uint8_t inputLength = 0;
bool discardLine = false;

volatile int64_t encoderTicks[4] = {};
volatile uint32_t encoderEdges[4] = {}, encoderInvalid[4] = {};
volatile uint8_t encoderState[4] = {};
portMUX_TYPE encoderLock = portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR const int8_t quadrature[16] =
  {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};

// Both pins in each configured encoder pair are in the same input bank.
uint8_t IRAM_ATTR sampleEncoder(uint8_t a, uint8_t b) {
  const bool high = a >= 32;
  const uint32_t bits = REG_READ(high ? GPIO_IN1_REG : GPIO_IN_REG);
  return (((bits >> (high ? a-32 : a)) & 1U) << 1) |
          ((bits >> (high ? b-32 : b)) & 1U);
}
void IRAM_ATTR encoderTick(uint8_t i) {
  portENTER_CRITICAL_ISR(&encoderLock);
  const uint8_t next = sampleEncoder(wheels[i].encA, wheels[i].encB);
  const int8_t delta = quadrature[(encoderState[i] << 2) | next];
  if ((encoderState[i] ^ next) == 3) encoderInvalid[i] = encoderInvalid[i] + 1;
  if (delta) {
    encoderTicks[i] = encoderTicks[i] + delta * wheels[i].encoderSign;
    encoderEdges[i] = encoderEdges[i] + 1;
  }
  encoderState[i] = next;
  portEXIT_CRITICAL_ISR(&encoderLock);
}
void IRAM_ATTR encFL() { encoderTick(0); }
void IRAM_ATTR encFR() { encoderTick(1); }
void IRAM_ATTR encRL() { encoderTick(2); }
void IRAM_ATTR encRR() { encoderTick(3); }

Reading readEncoder() {
  Reading r;
  portENTER_CRITICAL(&encoderLock);
  r.us = micros();
  r.ticks = encoderTicks[wheelIndex];
  r.edges = encoderEdges[wheelIndex];
  r.invalid = encoderInvalid[wheelIndex];
  portEXIT_CRITICAL(&encoderLock);
  return r;
}
uint8_t pwmBits() { return profile == Profile::History ? 8 : 9; }
uint32_t pwmDuty() { return uint32_t(duty256) << (pwmBits() - 8); }
const char *profileName() {
  return profile == Profile::History ? "history" :
         profile == Profile::Apb ? "apb" : "ref";
}
const char *clockName() {
  return profile == Profile::History ? "AUTO" :
         profile == Profile::Apb ? "APB" : "REF_TICK";
}
ledc_clk_cfg_t clockSetting() {
  return profile == Profile::History ? LEDC_AUTO_CLK :
         profile == Profile::Apb ? LEDC_USE_APB_CLK : LEDC_USE_REF_TICK;
}
bool validFrequency(uint32_t hz) {
  // Deliberately bounded range; REF / 9 bits tops out at 1953.125 Hz.
  return hz >= 200 && hz <= (profile == Profile::Ref ? 1900U : 20000U);
}

void forceGpioLow() {
  for (const Wheel &w : wheels) {
    for (uint8_t pin : {w.in1,w.in2}) {
      gpio_reset_pin(static_cast<gpio_num_t>(pin));
      gpio_set_level(static_cast<gpio_num_t>(pin),0);
      gpio_set_direction(static_cast<gpio_num_t>(pin),GPIO_MODE_OUTPUT);
    }
  }
}
// Own all eight IDF channels on high-speed timer 0. Never use Arduino's
// attach/detach wrapper: it can restore an old actual-duty register on attach.
// ledc_stop forces idle LOW immediately; it does not wait for duty-zero latch.
bool coastOutputs() {
  bool ok = true;
  for (uint8_t channel=0; channel<8; ++channel)
    if (channelReady[channel] &&
        ledc_stop(LEDC_HIGH_SPEED_MODE,static_cast<ledc_channel_t>(channel),0)!=ESP_OK)
      ok=false;
  if (!ok || !pwmInitialized) forceGpioLow();
  if (!ok) {
    hardwareFault=true;
    pwmInitialized=false; // fallback disconnected GPIOs; reset is required
  }
  return ok;
}
bool setTimer(uint32_t hz) {
  ledc_timer_config_t timer = {};
  timer.speed_mode=LEDC_HIGH_SPEED_MODE;
  timer.duty_resolution=static_cast<ledc_timer_bit_t>(pwmBits());
  timer.timer_num=LEDC_TIMER_0;
  timer.freq_hz=hz;
  timer.clk_cfg=clockSetting();
  return ledc_timer_config(&timer)==ESP_OK;
}
bool initializePwm() {
  if (!setTimer(248)) return false;
  for (uint8_t i=0;i<4;++i) for (uint8_t j=0;j<2;++j) {
    const uint8_t index=i*2+j;
    ledc_channel_config_t channel = {};
    channel.gpio_num=j==0 ? wheels[i].in1 : wheels[i].in2;
    channel.speed_mode=LEDC_HIGH_SPEED_MODE;
    channel.channel=static_cast<ledc_channel_t>(index);
    channel.intr_type=LEDC_INTR_DISABLE;
    channel.timer_sel=LEDC_TIMER_0;
    channel.duty=0;
    channel.hpoint=0;
    if (ledc_channel_config(&channel)!=ESP_OK) return false;
    channelReady[index]=true;
    if (ledc_stop(LEDC_HIGH_SPEED_MODE,channel.channel,0)!=ESP_OK) return false;
  }
  pwmInitialized=true;
  return coastOutputs();
}

void addSample(const Reading &r) {
  if (sampleCount >= MAX_SAMPLES) return;
  const uint32_t elapsed = r.us - commandUs;
  if (sampleCount && samples[sampleCount-1].us == elapsed) return;
  samples[sampleCount++] =
    {elapsed, r.ticks-baseline.ticks, r.edges-baseline.edges,
     r.invalid-baseline.invalid};
}
double rpm(int64_t ticks, uint32_t us) {
  return us ? double(ticks) * 60000000.0 /
                (wheels[wheelIndex].countsPerTurn * us) : 0.0;
}
void emitReport(const char *reason) {
  if (!sampleCount) return;
  const Sample &last = samples[sampleCount-1];
  int five = -1;
  for (uint16_t i=0; i<sampleCount; ++i)
    if (samples[i].us >= 5000000) { five = i; break; }
  // First sample at/after 5 s is the tail baseline. Not assumed steady state.
  const bool hasTail = five >= 0 && last.us > samples[five].us;
  const double fullRpm = rpm(last.ticks, last.us);
  Serial.printf("# RESULT sequence=%lu trial=%lu reason=%s\n",
                (unsigned long)sequenceNumber, (unsigned long)trialNumber, reason);
  Serial.println("summary,sequence,trial,block,wheel,dir,profile,clock_requested,bits,hz_requested,hz_readback,duty_counts,duty_denominator,off_before_us,elapsed_us,ticks,valid_edges,invalid,first_edge_observed_us,full_rpm,first5_rpm,first5_end_us,tail_rpm,tail_begin_us,tail_end_us,reason");
  Serial.printf("summary,%lu,%lu,%u,%s,%d,%s,%s,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lld,%lu,%lu,",
    (unsigned long)sequenceNumber,(unsigned long)trialNumber,trial.block,
    wheels[wheelIndex].name,trial.direction,profileName(),clockName(),pwmBits(),
    (unsigned long)trial.hz,(unsigned long)frequencyReadback,
    (unsigned long)pwmDuty(),(unsigned long)(1UL << pwmBits()),
    (unsigned long)restElapsedUs,(unsigned long)last.us,(long long)last.ticks,
    (unsigned long)last.edges,(unsigned long)last.invalid);
  if (firstEdgeUs) Serial.printf("%lu", (unsigned long)firstEdgeUs);
  Serial.printf(",%.6f,", fullRpm);
  if (five >= 0) Serial.printf("%.6f,%lu", rpm(samples[five].ticks,samples[five].us),
                              (unsigned long)samples[five].us);
  else Serial.print(",");
  Serial.print(",");
  if (hasTail) Serial.printf("%.6f,%lu,%lu",
    rpm(last.ticks-samples[five].ticks,last.us-samples[five].us),
    (unsigned long)samples[five].us,(unsigned long)last.us);
  else Serial.print(",,");
  Serial.printf(",%s\n",reason);
  Serial.println("sample,sequence,trial,t_us,ticks,valid_edges,invalid");
  for (uint16_t i=0; i<sampleCount; ++i) {
    const Sample &s = samples[i];
    Serial.printf("sample,%lu,%lu,%lu,%lld,%lu,%lu\n",
      (unsigned long)sequenceNumber,(unsigned long)trialNumber,
      (unsigned long)s.us,(long long)s.ticks,
      (unsigned long)s.edges,(unsigned long)s.invalid);
  }
}

void prepareRest() {
  const Reading r = readEncoder();
  phase = Phase::Rest;
  restBeginUs = lastStopUs;
  quietSinceUs = r.us;
  restEdges = r.edges;
  restInvalid = r.invalid;
}
void finishRun(const char *reason, bool continuePlan) {
  // Save endpoint BEFORE stopping, excluding measured coast-down counts.
  const Reading endpoint = readEncoder();
  addSample(endpoint);
  const bool stoppedOk = coastOutputs();
  lastStopUs = micros();
  phase = Phase::Idle;
  if (!stoppedOk) { reason = "stop_api_error"; continuePlan = false; }
  const bool more = continuePlan && planPosition < planCount;
  if (!more) planCount = planPosition = 0;
  else prepareRest();
  // No serial reporting while powered.
  emitReport(reason);
  if (!more) Serial.println("# IDLE: all outputs LOW");
}
void abortSequence(const char *reason) {
  if (phase == Phase::Running) finishRun(reason, false);
  else {
    coastOutputs();
    lastStopUs = micros();
    phase = Phase::Idle;
    planCount = planPosition = 0;
    Serial.printf("# STOP: %s\n", reason);
  }
}
bool configurePwm(uint32_t hz) {
  if (!coastOutputs() || hardwareFault || !pwmInitialized) return false;
  // All channel signals remain disabled/idle LOW during timer changes.
  if (!setTimer(hz)) return false;
  frequencyReadback=ledc_get_freq(LEDC_HIGH_SPEED_MODE,LEDC_TIMER_0);
  const uint32_t error=frequencyReadback>hz ? frequencyReadback-hz : hz-frequencyReadback;
  return frequencyReadback && error<=max(2UL,(unsigned long)(hz/100));
}
void startTrial() {
  trial = plan[planPosition++];
  ++trialNumber;
  if (!configurePwm(trial.hz)) {
    hardwareFault = true;
    abortSequence("pwm_setup_error");
    return;
  }
  Serial.printf("# RUN sequence=%lu trial=%lu block=%u/%u wheel=%s dir=%d hz=%lu profile=%s duty=%lu/%lu\n",
    (unsigned long)sequenceNumber,(unsigned long)trialNumber,trial.block,blocks,
    wheels[wheelIndex].name,trial.direction,(unsigned long)trial.hz,
    profileName(),(unsigned long)pwmDuty(),(unsigned long)(1UL << pwmBits()));
  // Timestamp is software command-relative, not a measured first PWM edge.
  baseline = readEncoder();
  commandUs = baseline.us;
  restElapsedUs = commandUs-lastStopUs;
  firstEdgeUs = 0; lastEdgeUs = commandUs;
  observedEdges=baseline.edges;
  sampleCount = 0; nextSampleUs = SAMPLE_US;
  addSample(baseline);
  phase = Phase::Running;
  const Wheel &w = wheels[wheelIndex];
  const ledc_channel_t active=static_cast<ledc_channel_t>(
    wheelIndex*2+(trial.direction*w.motorSign>0 ? 0 : 1));
  if (ledc_set_duty(LEDC_HIGH_SPEED_MODE,active,pwmDuty())!=ESP_OK ||
      ledc_update_duty(LEDC_HIGH_SPEED_MODE,active)!=ESP_OK) {
    hardwareFault = true; finishRun("pwm_write_error",false); return;
  }
}
uint32_t nextRandom() {
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState;
}
void buildPlan(const uint32_t *hz, uint8_t count, bool single, int8_t direction) {
  if (hardwareFault) {
    Serial.println("# ERROR: PWM fault latched; reset before testing."); return;
  }
  for (uint8_t i=0; i<count; ++i) if (!validFrequency(hz[i])) {
    Serial.println("# ERROR: frequency out of range for this profile; no run queued.");
    return;
  }
  const uint16_t required = single ? 1 : uint16_t(count)*2*blocks;
  if (!required || required > MAX_TRIALS) {
    Serial.println("# ERROR: plan too large."); return;
  }
  planCount = planPosition = 0;
  randomState = seed;
  if (single) plan[planCount++] = {hz[0],direction,1};
  else for (uint8_t block=1; block<=blocks; ++block) {
    const uint16_t begin = planCount;
    for (uint8_t i=0; i<count; ++i) {
      plan[planCount++] = {hz[i],1,block};
      plan[planCount++] = {hz[i],-1,block};
    }
    // Shuffle WITHIN each complete block, never across block boundaries.
    for (uint16_t i=planCount-1; i>begin; --i) {
      const uint16_t j = begin + nextRandom() % (i-begin+1);
      const Trial temp = plan[i]; plan[i] = plan[j]; plan[j] = temp;
    }
  }
  coastOutputs();
  if (hardwareFault) { abortSequence("stop_api_error"); return; }
  lastStopUs = micros();
  ++sequenceNumber;
  Serial.printf("# PLAN sequence=%lu seed=%lu trials=%u wheel=%s profile=%s duty256=%u run_us=%lu minimum_rest_us=%lu cpr=%.4f\n",
    (unsigned long)sequenceNumber,(unsigned long)seed,planCount,wheels[wheelIndex].name,
    profileName(),duty256,(unsigned long)RUN_US,(unsigned long)REST_US,
    wheels[wheelIndex].countsPerTurn);
  Serial.printf("# PINS in1=%u in2=%u encA=%u encB=%u motor_sign=%d encoder_sign=%d\n",
    wheels[wheelIndex].in1,wheels[wheelIndex].in2,wheels[wheelIndex].encA,
    wheels[wheelIndex].encB,wheels[wheelIndex].motorSign,wheels[wheelIndex].encoderSign);
  for (uint16_t i=0;i<planCount;++i)
    Serial.printf("# ORDER %u block=%u hz=%lu dir=%d\n",i+1,plan[i].block,
                  (unsigned long)plan[i].hz,plan[i].direction);
  prepareRest();
}
void showHelp() {
  Serial.println("# Commands (newline):");
  Serial.println("# run <200..20000> <f|r> : one timed trial");
  Serial.println("# sweep : 248,1000,5000,20000 Hz, both directions, shuffled blocks");
  Serial.println("# fine : 200,240,248,250,256,300,500,1000 Hz, both directions");
  Serial.println("# profile history|apb|ref : 8-bit AUTO | 9-bit APB | 9-bit REF");
  Serial.println("# wheel fl|fr|rl|rr | duty <1..254> (fraction /256) | blocks <1..10>");
  Serial.println("# seed <1..4294967295> | ? | stop");
  Serial.println("# x or ! = STOP without newline. Configuration only while IDLE.");
  Serial.println("# ref profile permits 200..1900 Hz; use run or fine, not sweep.");
  Serial.printf("# CONFIG wheel=%s profile=%s clock_requested=%s bits=%u duty=%lu/%lu blocks=%u seed=%lu\n",
    wheels[wheelIndex].name,profileName(),clockName(),pwmBits(),
    (unsigned long)pwmDuty(),(unsigned long)(1UL << pwmBits()),
    blocks,(unsigned long)seed);
}
bool parseNumber(const char *s, uint32_t &value) {
  if (!s || !*s) return false;
  uint64_t n=0;
  for (;*s;++s) {
    if (*s<'0' || *s>'9') return false;
    n=n*10+(*s-'0');
    if (n>UINT32_MAX) return false;
  }
  value=uint32_t(n); return true;
}
void handleCommand() {
  input[inputLength]='\0';
  char *tokens[4], *context=nullptr;
  uint8_t count=0;
  char *token=strtok_r(input," \t",&context);
  while (token && count<4) {
    tokens[count++]=token; token=strtok_r(nullptr," \t",&context);
  }
  if (!count) return;
  if (!strcmp(tokens[0],"stop") && count==1) {
    abortSequence("operator_stop"); return;
  }
  if (phase != Phase::Idle) {
    abortSequence("command_during_sequence");
    Serial.println("# Command ignored; issue it again while IDLE."); return;
  }
  uint32_t number=0;
  if (count==1 && !strcmp(tokens[0],"?")) { showHelp(); return; }
  if (count==1 && !strcmp(tokens[0],"sweep")) {
    const uint32_t frequencies[]={248,1000,5000,20000};
    buildPlan(frequencies,4,false,1); return;
  }
  if (count==1 && !strcmp(tokens[0],"fine")) {
    const uint32_t frequencies[]={200,240,248,250,256,300,500,1000};
    buildPlan(frequencies,8,false,1); return;
  }
  if (count==3 && !strcmp(tokens[0],"run") && parseNumber(tokens[1],number) &&
      (!strcmp(tokens[2],"f") || !strcmp(tokens[2],"r"))) {
    buildPlan(&number,1,true,tokens[2][0]=='f'?1:-1); return;
  }
  if (count==2 && !strcmp(tokens[0],"wheel")) {
    for (uint8_t i=0;i<4;++i) if (!strcmp(tokens[1],wheels[i].name)) {
      coastOutputs(); wheelIndex=i; showHelp(); return;
    }
  } else if (count==2 && !strcmp(tokens[0],"profile")) {
    if (!strcmp(tokens[1],"history")) profile=Profile::History;
    else if (!strcmp(tokens[1],"apb")) profile=Profile::Apb;
    else if (!strcmp(tokens[1],"ref")) profile=Profile::Ref;
    else { Serial.println("# ERROR: profile must be history, apb or ref."); return; }
    showHelp(); return;
  } else if (count==2 && parseNumber(tokens[1],number)) {
    if (!strcmp(tokens[0],"duty") && number>=1 && number<=254) duty256=number;
    else if (!strcmp(tokens[0],"blocks") && number>=1 && number<=10) blocks=number;
    else if (!strcmp(tokens[0],"seed") && number>=1) seed=number;
    else { Serial.println("# ERROR: unknown command or value out of range."); return; }
    showHelp(); return;
  }
  Serial.println("# ERROR: invalid command. Use ? for help.");
}
void pollSerial() {
  for (uint8_t n=0;n<32 && Serial.available();++n) {
    const char c=char(Serial.read());
    if (c=='x' || c=='X' || c=='!') {
      abortSequence("operator_stop");
      inputLength=0; discardLine=true; continue;
    }
    if (c=='\r') continue;
    if (c=='\n') {
      if (!discardLine && inputLength) handleCommand();
      inputLength=0; discardLine=false;
    } else if (!discardLine) {
      if (inputLength<sizeof(input)-1) input[inputLength++]=c;
      else {
        abortSequence("input_overflow"); inputLength=0; discardLine=true;
      }
    }
  }
}
void setup() {
  // Initialize outputs before starting any serial/peripheral work.
  coastOutputs();
  lastStopUs=micros();
  Serial.begin(115200);
  if (!initializePwm()) {
    hardwareFault=true;
    coastOutputs();
    forceGpioLow();
    Serial.println("# ERROR: PWM initialization failed; motion disabled until reset.");
  }
  void (*handlers[4])()={encFL,encFR,encRL,encRR};
  for (uint8_t i=0;i<4;++i) {
    pinMode(wheels[i].encA,wheels[i].encA>=34?INPUT:INPUT_PULLUP);
    pinMode(wheels[i].encB,wheels[i].encB>=34?INPUT:INPUT_PULLUP);
    encoderState[i]=sampleEncoder(wheels[i].encA,wheels[i].encB);
    attachInterrupt(digitalPinToInterrupt(wheels[i].encA),handlers[i],CHANGE);
    attachInterrupt(digitalPinToInterrupt(wheels[i].encB),handlers[i],CHANGE);
  }
  Serial.printf("# Maker_PWM_Frequency_Test v1 core=%s sdk=%s\n",
    ESP_ARDUINO_VERSION_STR,ESP.getSdkVersion());
  Serial.println("# Boot stopped. Raw counts primary; RPM uses historical wheel calibration.");
  Serial.println("# Frequency readback is integer peripheral data, NOT a scope measurement.");
  showHelp();
}
void loop() {
  pollSerial();
  const uint32_t now=micros();
  if (phase==Phase::Running) {
    const Reading r=readEncoder();
    const uint32_t elapsed=r.us-commandUs;
    // Observation latency is loop-resolution (~1 ms), not ISR edge timestamp.
    if (r.edges!=observedEdges) {
      if (!firstEdgeUs) firstEdgeUs=elapsed;
      lastEdgeUs=r.us;
      observedEdges=r.edges;
    }
    if (elapsed>=RUN_US) finishRun("complete",true);
    else if (r.us-lastEdgeUs>=NO_EDGE_US) finishRun("no_encoder_edges",false);
    else if (elapsed>=nextSampleUs) {
      addSample(r);
      nextSampleUs=(elapsed/SAMPLE_US+1)*SAMPLE_US;
    }
  } else if (phase==Phase::Rest) {
    const Reading r=readEncoder();
    if (r.edges!=restEdges || r.invalid!=restInvalid) {
      quietSinceUs=r.us; restEdges=r.edges; restInvalid=r.invalid;
    }
    if (now-restBeginUs>=REST_TIMEOUT_US) abortSequence("encoder_not_quiet");
    else if (now-restBeginUs>=REST_US && r.us-quietSinceUs>=QUIET_US) startTrial();
  }
  delay(1);
}
