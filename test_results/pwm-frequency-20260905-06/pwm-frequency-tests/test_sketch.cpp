// Tests compile and invoke the actual sketch, not a copied state machine.
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <map>
#include <set>
#include <tuple>
#include "../Maker_PWM_Frequency_Test/Maker_PWM_Frequency_Test.ino"

void require(bool condition,const std::string &message) {
  if(!condition) throw std::runtime_error(message);
}
bool has(const std::string &text) { return Serial.output.find(text)!=std::string::npos; }
void bytes(const std::string &s) {
  Serial.feed(s);
  while(Serial.available()) loop();
}
void command(const std::string &s) { bytes(s+"\n"); }
void edge() {
  const uint8_t next[4]={2,0,3,1};
  const uint8_t state=next[encoderState[wheelIndex]];
  fake::inputs[wheels[wheelIndex].encA]=(state>>1)&1;
  fake::inputs[wheels[wheelIndex].encB]=state&1;
  fake::handlers[wheels[wheelIndex].encA]();
}
void invalidEdge() {
  const uint8_t state=encoderState[wheelIndex]^3;
  fake::inputs[wheels[wheelIndex].encA]=(state>>1)&1;
  fake::inputs[wheels[wheelIndex].encB]=state&1;
  fake::handlers[wheels[wheelIndex].encA]();
}
void advance(unsigned ms,bool movement=false) {
  for(unsigned i=0;i<ms;++i) {
    if(movement && i%10==0) edge();
    loop();
  }
}
void waitRunning() {
  for(unsigned i=0;i<16000 && phase==Phase::Rest;++i) loop();
  require(phase==Phase::Running,"expected running after configured rest");
}
void run(const std::string &spec="248 f") { command("run "+spec); waitRunning(); }
void assertStopped() {
  require(phase==Phase::Idle,"not idle");
  require(fake::outputsLow(),"an output still energized");
  require(planCount==0 && planPosition==0,"queue was not cancelled");
}
std::vector<std::string> fields(const std::string &s) {
  std::vector<std::string> v; size_t begin=0;
  while(true) { size_t at=s.find(',',begin); v.push_back(s.substr(begin,at-begin));
    if(at==std::string::npos) return v;
    begin=at+1; }
}
void assertCsv(bool longRun) {
  std::istringstream lines(Serial.output); std::string line;
  std::vector<std::string> header,data; unsigned summaries=0,records=0;
  while(std::getline(lines,line)) {
    if(line.rfind("summary,",0)==0) {
      auto f=fields(line);
      if(f[1]=="sequence") header=f;
      else { require(f.size()==header.size(),"summary/header field count mismatch"); data=f; ++summaries; }
    }
    if(line.rfind("sample,",0)==0) {
      auto f=fields(line); require(f.size()==7,"sample CSV field count mismatch");
      if(f[1]!="sequence") ++records;
    }
  }
  require(summaries==1 && records>=2,"missing result or samples");
  auto field=[&](const char *key) { auto it=std::find(header.begin(),header.end(),key);
    require(it!=header.end(),std::string("missing CSV field ")+key); return data[it-header.begin()]; };
  if(longRun) {
    require(!field("first5_rpm").empty() && !field("tail_rpm").empty(),"long-run windows missing");
    require(field("reason")=="complete","long run not complete");
    require(std::stoul(field("elapsed_us"))>=RUN_US,"early timed stop");
  } else {
    require(field("first5_rpm").empty() && field("tail_rpm").empty(),"short run fabricated window");
  }
}
void boot() {
  require(fake::serialBeganAfterOutputsLow,"serial started before GPIOs were low");
  advance(20000); assertStopped(); require(fake::channelCalls==8,"boot did not configure exactly eight channels");
  require(fake::updateCalls==0,"boot energized an output");
  for(const auto &w:fake::writes) require(w.duty==0,"boot configured nonzero duty");
}
void timed() {
  run();
  require(fake::pins[wheels[0].in1].duty==175,"history duty not applied");
  require(fake::pins[wheels[0].in2].duty==0,"inactive direction not LOW");
  for(unsigned i=0;i<9000 && phase==Phase::Running;++i) { if(i%10==0) edge(); loop(); }
  assertStopped(); require(has("reason=complete"),"timed completion absent"); assertCsv(true);
  const auto ticks=samples[sampleCount-1].ticks; edge();
  require(samples[sampleCount-1].ticks==ticks,"coast-down altered saved endpoint");
}
void noedge() {
  command("sweep"); waitRunning(); advance(1600); assertStopped();
  require(has("reason=no_encoder_edges"),"no-edge watchdog did not abort"); assertCsv(false);
  const auto n=trialNumber; advance(20000); require(trialNumber==n,"watchdog allowed next trial");
}
void emergencyRest() {
  command("sweep"); require(phase==Phase::Rest,"plan not resting"); bytes("x");
  assertStopped(); advance(20000); require(trialNumber==0,"rest abort launched trial");
}
void emergencyRun() {
  command("sweep"); waitRunning(); advance(200,true); bytes("!"); assertStopped();
  require(has("reason=operator_stop"),"emergency reason missing"); assertCsv(false);
  auto n=trialNumber; advance(20000); require(trialNumber==n,"emergency failed to cancel queue");
}
void busyCommand() {
  run(); command("duty 150"); assertStopped(); require(duty256==175,"busy command mutated configuration");
  command("duty 150"); require(duty256==150,"idle retry ignored");
}
void failedSetup(const std::string &name) {
  if(name=="timerfailure") fake::failTimerAt=fake::timerCalls+1;
  if(name=="clockfailure") fake::failClock=true;
  command("sweep"); advance(3100); assertStopped();
  require(hardwareFault,"setup error not latched"); require(has("pwm_setup_error"),"setup reason missing");
  const auto n=fake::timerCalls; command("run 248 f"); advance(20000);
  assertStopped(); require(fake::timerCalls==n,"latched fault allowed retry");
}
void failedBoot() {
  assertStopped(); require(hardwareFault,"boot error not latched");
  require(has("initialization failed"),"boot failure not reported");
  const auto n=fake::timerCalls; command("sweep"); advance(20000);
  assertStopped(); require(fake::timerCalls==n,"failed initialization allowed motion");
  for(const auto &w:fake::writes) require(w.duty==0,"failed boot programmed nonzero output");
}
void writeFailure() {
  fake::failPositiveWrite=true; command("sweep"); advance(3100); assertStopped();
  require(hardwareFault && has("pwm_write_error"),"energize-write error not handled");
}
void updateFailure() {
  fake::failUpdateAt=fake::updateCalls+1; command("sweep"); advance(3100); assertStopped();
  require(hardwareFault && has("pwm_write_error"),"update-duty error not handled");
}
void stopFailure() {
  run(); fake::failStopAt=fake::stopCalls+1;
  bytes("!\n"); assertStopped(); require(hardwareFault && has("stop_api_error"),"stop failure not latched");
}
void readbackFailure() {
  fake::readbackOverride=0; command("run 248 f"); advance(3100); assertStopped();
  require(hardwareFault && has("pwm_setup_error"),"bad readback not handled");
  for(const auto &w:fake::writes) require(w.duty==0,"energized before validating frequency");
}
void parser() {
  command("duty 1"); require(duty256==1,"minimum duty rejected");
  command("duty 254"); require(duty256==254,"maximum duty rejected");
  for(auto s:{"duty 0","duty 255","duty -1","duty 4294967296","duty 1.5","duty 1 junk","duty +2"}) {
    command(s); require(duty256==254 && phase==Phase::Idle,"invalid duty changed state");
  }
  command("blocks 10"); require(blocks==10,"max blocks rejected");
  command("blocks 11"); require(blocks==10,"out-of-range blocks accepted");
  command("seed 4294967295"); require(seed==UINT32_MAX,"uint32 seed boundary rejected");
  command("seed 4294967296"); require(seed==UINT32_MAX,"seed overflow accepted");
  command("seed 0"); require(seed==UINT32_MAX,"zero seed accepted");
  for(auto s:{"run 199 f","run 20001 f","run -248 f","run 248 q","run 248 f junk","run 248 f junk fifth"}) {
    command(s); assertStopped();
  }
  for(auto s:{"run 200 f","run 20000 r"}) { command(s); require(phase==Phase::Rest,"valid run rejected"); bytes("!\n"); }
  command("profile ref"); command("run 1900 f"); require(phase==Phase::Rest,"REF valid max rejected"); bytes("!\n");
  command("run 1901 f"); assertStopped(); command("sweep"); assertStopped();
}
void overflow() {
  run(); bytes(std::string(110,'a')); assertStopped();
  require(has("input_overflow"),"overflow not reported"); bytes("run 248 f\n"); assertStopped();
}
using PlanKey=std::tuple<uint32_t,int,int>;
std::vector<PlanKey> snapshot() {
  std::vector<PlanKey> r; for(unsigned i=0;i<planCount;++i) r.emplace_back(plan[i].hz,plan[i].direction,plan[i].block); return r;
}
void balanced() {
  command("blocks 4"); command("seed 98765"); command("sweep");
  require(planCount==32,"wrong complete-block size"); const auto first=snapshot();
  for(unsigned b=1;b<=4;++b) {
    std::map<std::pair<uint32_t,int>,int> seen;
    for(unsigned i=(b-1)*8;i<b*8;++i) {
      require(plan[i].block==b,"shuffle crossed block boundary"); ++seen[{plan[i].hz,plan[i].direction}];
    }
    for(auto hz:{248u,1000u,5000u,20000u}) for(int d:{-1,1}) require(seen[{hz,d}]==1,"unbalanced condition");
  }
  bytes("!\n"); command("sweep"); require(snapshot()==first,"seed not reproducible");
  bytes("!\n"); command("seed 98766"); command("sweep"); require(snapshot()!=first,"different seed left same order");
  bytes("!\n"); command("blocks 10"); command("fine"); require(planCount==160,"maximum fine plan wrong");
}
void scaling() {
  for(const char *p:{"history","apb","ref"}) {
    command(std::string("profile ")+p);
    for(unsigned d:{1u,148u,150u,175u,254u}) {
      command("duty "+std::to_string(d));
      require(uint64_t(pwmDuty())*256==uint64_t(d)*(1u<<pwmBits()),"duty fraction drifted");
    }
  }
  command("profile apb"); command("duty 175"); run("248 r");
  require(fake::clock==LEDC_USE_APB_CLK && pwmBits()==9,"APB profile not installed");
  require(fake::pins[wheels[0].in2].duty==350 && fake::pins[wheels[0].in1].duty==0,"reverse drive/scaling wrong");
}
void restart() {
  run("248 f"); advance(200,true); bytes("!\n"); assertStopped();
  const auto &w=wheels[0];
  require(fake::pins[w.in1].duty==175 && !fake::pins[w.in1].enabled,"stub did not retain stopped duty register");
  command("profile apb"); run("5000 r");
  require(!fake::pins[w.in1].enabled,"old forward duty reappeared during reverse restart");
  require(fake::pins[w.in2].enabled && fake::pins[w.in2].duty==350,"new reverse output incorrect");
  for(unsigned i=1;i<4;++i) require(!fake::pins[wheels[i].in1].enabled && !fake::pins[wheels[i].in2].enabled,"unselected wheel enabled");
  require(fake::unsafeTimerChanges==0,"timer changed while motor output active");
}
void queueProgression() {
  command("blocks 1"); command("sweep"); waitRunning();
  const auto firstNumber=trialNumber;
  for(unsigned i=0;i<9000 && phase==Phase::Running;++i) { if(i%10==0) edge(); loop(); }
  require(phase==Phase::Rest && fake::outputsLow(),"completed trial failed to enter stopped rest");
  const uint32_t stop=fake::now;
  while(phase==Phase::Rest) loop();
  require(phase==Phase::Running && trialNumber==firstNumber+1,"next queued trial did not start");
  require(fake::now-stop>=REST_US,"rest between queued trials too short");
  require(fake::unsafeTimerChanges==0,"queued frequency change occurred under power");
  bytes("x\n"); assertStopped();
}
void quietTimeout() {
  command("sweep"); for(unsigned i=0;i<16000 && phase==Phase::Rest;++i) { if(i%100==0) invalidEdge(); loop(); }
  assertStopped(); require(trialNumber==0 && has("encoder_not_quiet"),"rest noise did not inhibit start");
}
void quadratureTest() {
  for(int i=0;i<4;++i) edge();
  require(encoderTicks[0]==4 && encoderEdges[0]==4,"valid quadrature decoding wrong");
  invalidEdge(); require(encoderInvalid[0]==1 && encoderTicks[0]==4,"invalid edge changed ticks");
  run(); advance(200,true); auto before=encoderEdges[0];
  for(unsigned i=0;i<1600 && phase==Phase::Running;++i) { if(i%20==0) invalidEdge(); loop(); }
  assertStopped(); require(encoderEdges[0]==before,"invalid transitions treated as valid");
  require(has("no_encoder_edges"),"invalid transitions kept watchdog alive");
}
int main(int argc,char **argv) {
  try {
    require(argc==2,"one test name required"); std::string name=argv[1];
    if(name=="wrap") fake::now=UINT32_MAX-4000;
    if(name=="boot_channel1") fake::failChannelAt=1;
    if(name=="boot_channel2") fake::failChannelAt=2;
    if(name=="boot_channel8") fake::failChannelAt=8;
    if(name=="boot_timer") fake::failTimerAt=1;
    if(name=="boot_stop") fake::failStopAt=1;
    setup();
    if(name=="boot") boot(); else if(name=="timed" || name=="wrap") timed();
    else if(name.rfind("boot_",0)==0) failedBoot();
    else if(name=="noedge") noedge(); else if(name=="emergency_rest") emergencyRest();
    else if(name=="emergency_run") emergencyRun(); else if(name=="busy_command") busyCommand();
    else if(name=="timerfailure" || name=="clockfailure") failedSetup(name);
    else if(name=="writefailure") writeFailure(); else if(name=="stopfailure") stopFailure();
    else if(name=="updatefailure") updateFailure(); else if(name=="readbackfailure") readbackFailure();
    else if(name=="parser") parser(); else if(name=="overflow") overflow();
    else if(name=="balanced") balanced(); else if(name=="scaling") scaling();
    else if(name=="quiet_timeout") quietTimeout(); else if(name=="quadrature") quadratureTest();
    else if(name=="restart") restart(); else if(name=="queue_progression") queueProgression();
    else throw std::runtime_error("unknown test "+name);
    std::cout<<"PASS "<<name<<"\n"; return 0;
  } catch(const std::exception &e) { std::cerr<<"FAIL "<<argv[1]<<": "<<e.what()<<"\n"; return 1; }
}
