#include "OttoI2R.h"
#include <Preferences.h>

// ── 핀 번호 (로봇이 바뀌어도 보통 같다) ──────────────────────
static const int YL_PIN = 4;
static const int YR_PIN = 5;
static const int RL_PIN = 6;
static const int RR_PIN = 7;

// ── 0도/180도 펄스폭, 정지값 (하드웨어로 맞추는 값, 보통 같다) ──
static const int PULSE_MIN  = 500;
static const int PULSE_MAX  = 2400;
static const int RL_STOP_US = 1500;
static const int RR_STOP_US = 1500;
static const int NEUTRAL    = 90;

// ── 서보 제어 (ESP32 기본 PWM 'LEDC'을 직접 쓴다. 외부 라이브러리 필요 없음) ──────────
// 서보는 20ms(50Hz)마다 펄스 하나를 받는다. 펄스 길이(us)가 곧 각도 또는 속도다.
// 14비트 해상도로 20ms 주기를 16384 단계로 나눠서, 원하는 펄스 길이를 단계 수(duty)로 바꿔 내보낸다.
// 핀마다 PWM 채널이 따로 배정되므로 서보 4개를 동시에 움직여도 신호가 겹치지 않는다.
// 필요한 기능은 attach / detach / writeMicroseconds 세 가지뿐이다. (ESP32 보드 패키지 3.0 이상)
class OttoServo {
 public:
  bool attach(int pin, int minUs, int maxUs) {
    if (_attached && _pin != pin) detach();
    _minUs = minUs;
    _maxUs = maxUs;
    if (!_attached) {
      if (!ledcAttach(pin, PWM_HZ, PWM_BITS)) return false;  // 채널은 보드 패키지가 알아서 고른다
      _pin = pin;
      _attached = true;
    }
    return true;
  }
  void detach() {
    if (!_attached) return;
    ledcDetach(_pin);
    _attached = false;
  }
  void writeMicroseconds(int us) {
    if (!_attached) return;
    us = constrain(us, _minUs, _maxUs);
    ledcWrite(_pin, (uint32_t)us * PWM_STEPS / PWM_PERIOD_US);
  }

 private:
  static const uint32_t PWM_HZ = 50;
  static const uint8_t PWM_BITS = 14;
  static const uint32_t PWM_STEPS = 1UL << PWM_BITS;  // 16384
  static const uint32_t PWM_PERIOD_US = 20000;        // 50Hz = 20ms
  int _pin = -1;
  int _minUs = 500;
  int _maxUs = 2400;
  bool _attached = false;
};

static OttoServo servoYL;
static OttoServo servoYR;
static OttoServo servoRL;
static OttoServo servoRR;

// ottoInit()에서 받아서 저장해 두는, 로봇마다 다른 실측값
static int s_ylPulseCenter;
static int s_yrPulseCenter;

// 각도(0~180)를 보정된 펄스폭(us)으로 바꾼다 (0~90, 90~180 구간을 따로 선형보간)
static int angleToPulse(int angle, int pulseCenter) {
  if (angle <= 90) {
    return map(angle, 0, 90, PULSE_MIN, pulseCenter);
  } else {
    return map(angle, 90, 180, pulseCenter, PULSE_MAX);
  }
}

// 엉덩이가 끝(YR 0도, YL 180도)에 가 있는지 나타낸다. rolling 동작이 한 번만 돌리려고 쓴다.
// 다른 동작이 엉덩이를 움직이면 writeYL/writeYR이 false로 되돌려서, 다음 rolling 때 다시 돌린다.
static bool s_hipsAtEnds = false;

void writeYL(int angle) {
  s_hipsAtEnds = false;
  servoYL.writeMicroseconds(angleToPulse(angle, s_ylPulseCenter));
}
void writeYR(int angle) {
  s_hipsAtEnds = false;
  servoYR.writeMicroseconds(angleToPulse(angle, s_yrPulseCenter));
}

void ottoInit(int ylPulseCenter, int yrPulseCenter) {
  s_ylPulseCenter = ylPulseCenter;
  s_yrPulseCenter = yrPulseCenter;

  Serial.begin(115200);
  servoYL.attach(YL_PIN, PULSE_MIN, PULSE_MAX);
  servoYR.attach(YR_PIN, PULSE_MIN, PULSE_MAX);
  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(500);
}

// 보정값. 아무것도 등록하지 않아도 동작하도록 기본값으로 시작한다. (한 대의 로봇에서 실측한 값)
// ottoSetCalibration()으로 바꾸거나, ottoInit()/ottoLoadCalibration()이 보드에 저장된 값을 불러와 덮어쓴다.
static const int DEF_YL_PULSE_CENTER = 1250;
static const int DEF_YR_PULSE_CENTER = 1450;
static const int DEF_TILT_OFFSET_R = 20;
static const int DEF_TILT_OFFSET_L = 23;
static const int DEF_RL_SPEED_US = 450;
static const int DEF_RR_SPEED_US = 550;

static int s_ylCenterCfg = DEF_YL_PULSE_CENTER;
static int s_yrCenterCfg = DEF_YR_PULSE_CENTER;
static int s_tiltOffsetR = DEF_TILT_OFFSET_R;
static int s_tiltOffsetL = DEF_TILT_OFFSET_L;
static int s_rlSpeedUs = DEF_RL_SPEED_US;
static int s_rrSpeedUs = DEF_RR_SPEED_US;
static bool s_walkStarted = false;

// 값의 허용 범위 (잘못된 값으로 서보와 기구가 상하지 않게 한다)
static const int CAL_CENTER_MIN = 1000, CAL_CENTER_MAX = 1900;
static const int CAL_TILT_MIN = 0, CAL_TILT_MAX = 45;
static const int CAL_SPEED_MIN = 0, CAL_SPEED_MAX = 600;
static const char *CAL_NS = "ottocal";  // NVS 이름 (Wi-Fi 설정 등 다른 값과 섞이지 않는다)

// 서보는 움직이지 않고 값만 저장한다. (전역 객체 생성 시점에 호출되므로 하드웨어를 건드리지 않는다)
// 서보가 이미 켜져 있으면 다리 중심값도 같이 바꿔서, 다음에 다리를 움직일 때부터 바로 적용된다.
void ottoSetCalibration(int ylPulseCenter, int yrPulseCenter, int tiltOffsetR, int tiltOffsetL, int rlSpeedUs, int rrSpeedUs) {
  s_ylCenterCfg = ylPulseCenter;
  s_yrCenterCfg = yrPulseCenter;
  s_tiltOffsetR = tiltOffsetR;
  s_tiltOffsetL = tiltOffsetL;
  s_rlSpeedUs = rlSpeedUs;
  s_rrSpeedUs = rrSpeedUs;
  if (s_walkStarted) {
    s_ylPulseCenter = ylPulseCenter;
    s_yrPulseCenter = yrPulseCenter;
  }
}

void ottoGetCalibration(OttoCalibration &out) {
  out.ylCenter = s_ylCenterCfg;
  out.yrCenter = s_yrCenterCfg;
  out.tiltR = s_tiltOffsetR;
  out.tiltL = s_tiltOffsetL;
  out.rlSpeed = s_rlSpeedUs;
  out.rrSpeed = s_rrSpeedUs;
}

void ottoSetHipCenter(int ylPulseCenter, int yrPulseCenter) {
  ylPulseCenter = constrain(ylPulseCenter, CAL_CENTER_MIN, CAL_CENTER_MAX);
  yrPulseCenter = constrain(yrPulseCenter, CAL_CENTER_MIN, CAL_CENTER_MAX);
  ottoSetCalibration(ylPulseCenter, yrPulseCenter, s_tiltOffsetR, s_tiltOffsetL, s_rlSpeedUs, s_rrSpeedUs);
}

void ottoSetTilt(int tiltOffsetR, int tiltOffsetL) {
  tiltOffsetR = constrain(tiltOffsetR, CAL_TILT_MIN, CAL_TILT_MAX);
  tiltOffsetL = constrain(tiltOffsetL, CAL_TILT_MIN, CAL_TILT_MAX);
  ottoSetCalibration(s_ylCenterCfg, s_yrCenterCfg, tiltOffsetR, tiltOffsetL, s_rlSpeedUs, s_rrSpeedUs);
}

void ottoSetSpeed(int rlSpeedUs, int rrSpeedUs) {
  rlSpeedUs = constrain(rlSpeedUs, CAL_SPEED_MIN, CAL_SPEED_MAX);
  rrSpeedUs = constrain(rrSpeedUs, CAL_SPEED_MIN, CAL_SPEED_MAX);
  ottoSetCalibration(s_ylCenterCfg, s_yrCenterCfg, s_tiltOffsetR, s_tiltOffsetL, rlSpeedUs, rrSpeedUs);
}

static bool calInRange(int v, int lo, int hi) { return v >= lo && v <= hi; }

bool ottoHasSavedCalibration() {
  Preferences p;
  if (!p.begin(CAL_NS, true)) return false;
  bool ok = p.getUChar("ok", 0) == 1;
  p.end();
  return ok;
}

bool ottoLoadCalibration() {
  Preferences p;
  if (!p.begin(CAL_NS, true)) return false;
  bool saved = p.getUChar("ok", 0) == 1;
  int yl = p.getInt("yl", -1), yr = p.getInt("yr", -1);
  int tr = p.getInt("tr", -1), tl = p.getInt("tl", -1);
  int rl = p.getInt("rl", -1), rr = p.getInt("rr", -1);
  p.end();
  if (!saved) return false;
  // 범위를 벗어난 값(깨졌거나 이상한 값)은 쓰지 않는다
  if (!calInRange(yl, CAL_CENTER_MIN, CAL_CENTER_MAX) || !calInRange(yr, CAL_CENTER_MIN, CAL_CENTER_MAX) ||
      !calInRange(tr, CAL_TILT_MIN, CAL_TILT_MAX) || !calInRange(tl, CAL_TILT_MIN, CAL_TILT_MAX) ||
      !calInRange(rl, CAL_SPEED_MIN, CAL_SPEED_MAX) || !calInRange(rr, CAL_SPEED_MIN, CAL_SPEED_MAX))
    return false;
  ottoSetCalibration(yl, yr, tr, tl, rl, rr);
  return true;
}

// 인자 없는 초기화: 보드에 저장된 보정값이 있으면 불러온 뒤(없으면 기본값) 서보를 초기화한다.
// 두 번 불러도 처음 한 번만 동작한다. 값을 먼저 불러온 뒤 서보를 붙이므로 다리가 두 번 움직이지 않는다.
void ottoInit() {
  if (s_walkStarted) return;
  ottoLoadCalibration();
  ottoInit(s_ylCenterCfg, s_yrCenterCfg);
  s_walkStarted = true;
}

bool ottoSaveCalibration() {
  Preferences p;
  if (!p.begin(CAL_NS, false)) return false;
  size_t n = 0;
  n += p.putInt("yl", s_ylCenterCfg);
  n += p.putInt("yr", s_yrCenterCfg);
  n += p.putInt("tr", s_tiltOffsetR);
  n += p.putInt("tl", s_tiltOffsetL);
  n += p.putInt("rl", s_rlSpeedUs);
  n += p.putInt("rr", s_rrSpeedUs);
  n += p.putUChar("ok", 1);
  p.end();
  return n > 0;
}

void ottoClearCalibration() {
  Preferences p;
  if (!p.begin(CAL_NS, false)) return;
  p.clear();
  p.end();
}

// ---------------------------------------------------------------
// 360도 회전 테스트 (두 바퀴 동시 회전)
// ---------------------------------------------------------------
void ottoCalibrationRolling() {
  // 처음 호출될 때 한 번만 서보와 시리얼을 초기화한다
  if (!s_walkStarted) {
    ottoInit(s_ylCenterCfg, s_yrCenterCfg);
    s_walkStarted = true;
  }

  int rlSpeedUs = s_rlSpeedUs;
  int rrSpeedUs = s_rrSpeedUs;
  servoRL.attach(RL_PIN, PULSE_MIN, PULSE_MAX);
  servoRR.attach(RR_PIN, PULSE_MIN, PULSE_MAX);
  servoRL.writeMicroseconds(RL_STOP_US);
  servoRR.writeMicroseconds(RR_STOP_US);

  Serial.println("0도 / 180도");
  writeYR(0);   // 하드웨어로 맞춘 0도 끝단
  writeYL(180); // 하드웨어로 맞춘 180도 끝단
  delay(3000);

  Serial.println("▶ 전진");
  servoRL.writeMicroseconds(RL_STOP_US + rlSpeedUs);
  servoRR.writeMicroseconds(RR_STOP_US - rrSpeedUs);
  delay(1000);

  Serial.println("⏹ 정지");
  servoRL.writeMicroseconds(RL_STOP_US);
  servoRR.writeMicroseconds(RR_STOP_US);
  delay(2000);

  Serial.println("◀ 후진");
  servoRL.writeMicroseconds(RL_STOP_US - rlSpeedUs);
  servoRR.writeMicroseconds(RR_STOP_US + rrSpeedUs);
  delay(1000);

  Serial.println("⏹ 정지");
  servoRL.writeMicroseconds(RL_STOP_US);
  servoRR.writeMicroseconds(RR_STOP_US);
  delay(2000);

  Serial.println("90도");
  writeYR(90);  // 보정된 중립 위치로 이동
  writeYL(90);
  delay(3000);
}

// ---------------------------------------------------------------
// 걷기 테스트 (오른발 -> 왼발 번갈아 한 걸음씩)
// ---------------------------------------------------------------
// 발 서보가 붙어 있는지 표시한다. footStop()이 발을 떼면 false로 되돌려서, 다음 명령 때 다시 붙이게 한다.
static bool s_slideAttached = false;

static void footStop(OttoServo &servo, int pin) {
  servo.detach();
  s_slideAttached = false;   // 떼었으니 다음 호출 때 두 발을 다시 붙인다
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

// ── 걷는 속도·보폭 (고정값) ──────────────────────────────────
// 걷는 모양이나 속도를 바꾸고 싶으면 아래 값을 수정한다.
// (로봇마다 실측해야 하는 TILT_OFFSET_R/L과 달리, 이 값들은 "걸음걸이 자체의 느낌"을
//  정하는 값이라 보통 로봇이 바뀌어도 크게 안 바뀐다.)
static const int WALK_RL_SPEED_US   = 500;  // 왼발 속도
static const int WALK_RR_SPEED_US   = 500;  // 오른발 속도
static const int WALK_TILT_TIME_MS  = 250;  // 다리를 기울이는 시간 - 늘리면 더 확실히 들림
static const int WALK_STEP_TIME_MS  = 150;  // 발을 굴리는 시간 - 늘리면 보폭이 커짐
static const int WALK_NEUTRAL_HOLD_MS = 100; // 중립 위치에서 멈춰 있는 시간
static const int WALK_WAIT_MS       = 30;   // 한 발 내딛은 뒤 다음 발로 넘어가기 전 대기 시간 - 늘리면 천천히 걸음
static const int WALK_VELOCITY_MIN = 20;   // walkFront(velocity)의 최소 속도(%)
static const int WALK_VELOCITY_MAX = 200;  // walkFront(velocity)의 최대 속도(%)
// velocity가 100을 넘으면 기다리는 시간만 줄인다. 아래는 줄일 수 있는 하한(ms)이다.
// 기울이는 시간이 너무 짧으면 서보가 각도에 닿기 전에 다음 동작이 시작되어 발이 안 들린다.
static const int WALK_TILT_MIN_MS = 150;   // 다리를 기울이는 시간의 하한
static const int WALK_HOLD_MIN_MS = 50;    // 중립에서 멈춰 있는 시간의 하한
static const int WALK_WAIT_MIN_MS = 10;    // 발 사이 대기 시간의 하한
// 미끄러짐(slide) 기울기: 걷기 기울기(보정값)와 따로 정해 둔 고정값이다.
// 보정값(걷기 기울기 20/23)을 그대로 쓰면 미끄러짐이 너무 크게 기울어서, 미끄러짐은 이 값을 쓴다.
static const int SLIDE_TILT_R = 5;
static const int SLIDE_TILT_L = 6;
// rollingFront(velocity)의 속도 범위(%)와, 서보 신호 범위 안에서 낼 수 있는 가장 큰 속도 값(us)
static const int ROLL_VELOCITY_MIN = 20;
static const int ROLL_VELOCITY_MAX = 200;
static const int ROLL_MAX_OFFSET_US = 900;   // 정지(1500us)에서 2400us까지

void ottoCalibrationWalk() {
  // 처음 호출될 때 한 번만 서보와 시리얼을 초기화한다
  if (!s_walkStarted) {
    ottoInit(s_ylCenterCfg, s_yrCenterCfg);
    s_walkStarted = true;
  }

  int tiltOffsetR = s_tiltOffsetR;
  int tiltOffsetL = s_tiltOffsetL;

  // 오른발 내딛기: 오른쪽으로 기울여서 오른발을 띄운다
  writeYL(NEUTRAL - tiltOffsetR);
  writeYR(NEUTRAL - tiltOffsetR);
  delay(WALK_TILT_TIME_MS);

  Serial.println("▶ 오른발 내딛기");
  servoRR.attach(RR_PIN, PULSE_MIN, PULSE_MAX);
  servoRR.writeMicroseconds(RR_STOP_US - WALK_RR_SPEED_US);
  delay(WALK_STEP_TIME_MS);
  footStop(servoRR, RR_PIN);

  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(WALK_NEUTRAL_HOLD_MS);
  delay(WALK_WAIT_MS);

  // 왼발 내딛기: 왼쪽으로 기울여서 왼발을 띄운다
  writeYL(NEUTRAL + tiltOffsetL);
  writeYR(NEUTRAL + tiltOffsetL);
  delay(WALK_TILT_TIME_MS);

  Serial.println("▶ 왼발 내딛기");
  servoRL.attach(RL_PIN, PULSE_MIN, PULSE_MAX);
  servoRL.writeMicroseconds(RL_STOP_US + WALK_RL_SPEED_US);
  delay(WALK_STEP_TIME_MS);
  footStop(servoRL, RL_PIN);

  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(WALK_NEUTRAL_HOLD_MS);
  delay(WALK_WAIT_MS);
}

// ---------------------------------------------------------------
// 직진 걷기 (오른발 -> 왼발 번갈아 앞으로)
// ---------------------------------------------------------------
// 속도(velocity)로 걷기. 20~200(%): 100이 보정한 기본 속도다.
// - 100 이하: 발의 회전 속도를 비율만큼 줄이고, 줄인 만큼 발을 굴리는 시간과 쉬는 시간을 늘려서 보폭은 비슷하게 유지한다.
//   (50이면 절반 속도. 서보는 너무 느리면 힘이 약해져 못 걷는 경우가 있어 20 미만은 20으로 맞춘다)
// - 100 초과: 발 회전 속도와 발 굴리는 시간(보폭)은 그대로 두고, 기울이기/중립/대기 시간만 줄여서 걸음 주기를 빠르게 한다.
//   (발 속도를 키우면 왼발과 오른발의 직진 비율이 깨지므로 건드리지 않는다. 시간은 위의 하한 아래로 줄이지 않는다)
void walkFront(int velocity) {
  // 처음 호출될 때 한 번만 서보와 시리얼을 초기화한다
  if (!s_walkStarted) {
    ottoInit(s_ylCenterCfg, s_yrCenterCfg);
    s_walkStarted = true;
  }

  velocity = constrain(velocity, WALK_VELOCITY_MIN, WALK_VELOCITY_MAX);

  int tiltOffsetR = s_tiltOffsetR;
  int tiltOffsetL = s_tiltOffsetL;
  int rlSpeedUs, rrSpeedUs, tiltMs, stepMs, holdMs, waitMs;
  if (velocity <= 100) {
    // 속도가 낮을수록 발 속도는 줄이고 시간은 늘린다 (velocity 100이면 원래 값 그대로)
    rlSpeedUs = s_rlSpeedUs * velocity / 100;
    rrSpeedUs = s_rrSpeedUs * velocity / 100;
    tiltMs = WALK_TILT_TIME_MS * 100 / velocity;
    stepMs = WALK_STEP_TIME_MS * 100 / velocity;
    holdMs = WALK_NEUTRAL_HOLD_MS * 100 / velocity;
    waitMs = WALK_WAIT_MS * 100 / velocity;
  } else {
    // 100을 넘으면 발 속도와 굴리는 시간은 그대로, 기다리는 시간만 줄인다 (하한 아래로는 줄이지 않는다)
    rlSpeedUs = s_rlSpeedUs;
    rrSpeedUs = s_rrSpeedUs;
    stepMs = WALK_STEP_TIME_MS;
    tiltMs = max(WALK_TILT_MIN_MS, WALK_TILT_TIME_MS * 100 / velocity);
    holdMs = max(WALK_HOLD_MIN_MS, WALK_NEUTRAL_HOLD_MS * 100 / velocity);
    waitMs = max(WALK_WAIT_MIN_MS, WALK_WAIT_MS * 100 / velocity);
  }

  // 오른발 내딛기: 오른쪽으로 기울여서 오른발을 띄우고, 역회전으로 앞으로 민다
  writeYL(NEUTRAL - tiltOffsetR);
  writeYR(NEUTRAL - tiltOffsetR);
  delay(tiltMs);

  servoRR.attach(RR_PIN, PULSE_MIN, PULSE_MAX);
  servoRR.writeMicroseconds(RR_STOP_US - rrSpeedUs);
  delay(stepMs);
  footStop(servoRR, RR_PIN);

  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(holdMs);
  delay(waitMs);

  // 왼발 내딛기: 왼쪽으로 기울여서 왼발을 띄우고, 정회전으로 앞으로 민다
  writeYL(NEUTRAL + tiltOffsetL);
  writeYR(NEUTRAL + tiltOffsetL);
  delay(tiltMs);

  servoRL.attach(RL_PIN, PULSE_MIN, PULSE_MAX);
  servoRL.writeMicroseconds(RL_STOP_US + rlSpeedUs);
  delay(stepMs);
  footStop(servoRL, RL_PIN);

  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(holdMs);
  delay(waitMs);
}

// 기본 속도(100)로 걷기
void walkFront() {
  walkFront(100);
}

// ---------------------------------------------------------------
// 뒤로 걷기 (오른발 -> 왼발 번갈아 뒤로)
// 들린 발이 아니라 바닥에 체중이 실린 반대쪽 발을 굴린다. (walk_back 2차 시도 방식)
// ---------------------------------------------------------------
// 속도(velocity)로 뒤로 걷기. 20~200(%): 100이 보정한 기본 속도다. (walkFront(velocity)와 같은 규칙)
// - 100 이하: 발 속도를 비율만큼 줄이고 시간은 늘려서 보폭을 비슷하게 유지한다. (20 미만은 20으로)
// - 100 초과: 발 속도와 굴리는 시간(보폭)은 그대로 두고, 기울이기/중립/대기 시간만 하한까지 줄인다.
void walkBack(int velocity) {
  // 처음 호출될 때 한 번만 서보와 시리얼을 초기화한다
  if (!s_walkStarted) {
    ottoInit(s_ylCenterCfg, s_yrCenterCfg);
    s_walkStarted = true;
  }

  velocity = constrain(velocity, WALK_VELOCITY_MIN, WALK_VELOCITY_MAX);

  int tiltOffsetR = s_tiltOffsetR;
  int tiltOffsetL = s_tiltOffsetL;
  int rlSpeedUs, rrSpeedUs, tiltMs, stepMs, holdMs, waitMs;
  if (velocity <= 100) {
    rlSpeedUs = s_rlSpeedUs * velocity / 100;
    rrSpeedUs = s_rrSpeedUs * velocity / 100;
    tiltMs = WALK_TILT_TIME_MS * 100 / velocity;
    stepMs = WALK_STEP_TIME_MS * 100 / velocity;
    holdMs = WALK_NEUTRAL_HOLD_MS * 100 / velocity;
    waitMs = WALK_WAIT_MS * 100 / velocity;
  } else {
    rlSpeedUs = s_rlSpeedUs;
    rrSpeedUs = s_rrSpeedUs;
    stepMs = WALK_STEP_TIME_MS;
    tiltMs = max(WALK_TILT_MIN_MS, WALK_TILT_TIME_MS * 100 / velocity);
    holdMs = max(WALK_HOLD_MIN_MS, WALK_NEUTRAL_HOLD_MS * 100 / velocity);
    waitMs = max(WALK_WAIT_MIN_MS, WALK_WAIT_MS * 100 / velocity);
  }

  // 오른발 내딛기: 오른쪽으로 기울여 한쪽 발을 띄우고, 바닥에 디딘 오른발(RR)을 뒤로 굴린다
  writeYL(NEUTRAL - tiltOffsetR);
  writeYR(NEUTRAL - tiltOffsetR);
  delay(tiltMs);

  servoRR.attach(RR_PIN, PULSE_MIN, PULSE_MAX);
  servoRR.writeMicroseconds(RR_STOP_US + rrSpeedUs);
  delay(stepMs);
  footStop(servoRR, RR_PIN);

  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(holdMs);
  delay(waitMs);

  // 왼발 내딛기: 왼쪽으로 기울여 한쪽 발을 띄우고, 바닥에 디딘 왼발(RL)을 뒤로 굴린다
  writeYL(NEUTRAL + tiltOffsetL);
  writeYR(NEUTRAL + tiltOffsetL);
  delay(tiltMs);

  servoRL.attach(RL_PIN, PULSE_MIN, PULSE_MAX);
  servoRL.writeMicroseconds(RL_STOP_US - rlSpeedUs);
  delay(stepMs);
  footStop(servoRL, RL_PIN);

  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(holdMs);
  delay(waitMs);
}

// 기본 속도(100)로 뒤로 걷기
void walkBack() {
  walkBack(100);
}

// ---------------------------------------------------------------
// 왼쪽으로 돌며 걷기 (오른발 보폭은 그대로, 왼발 보폭은 절반)
// 오른발이 더 멀리 나가므로 몸이 왼쪽으로 돈다.
// ---------------------------------------------------------------
void walkLeft() {
  // 처음 호출될 때 한 번만 서보와 시리얼을 초기화한다
  if (!s_walkStarted) {
    ottoInit(s_ylCenterCfg, s_yrCenterCfg);
    s_walkStarted = true;
  }

  int tiltOffsetR = s_tiltOffsetR;
  int rlSpeedUs = s_rlSpeedUs;
  int rrSpeedUs = s_rrSpeedUs;

  // 오른발 내딛기 (보폭 그대로): YL은 90도에 고정, YR만 반대 방향으로 기울여 오른발을 든다
  writeYL(NEUTRAL);
  writeYR(NEUTRAL + tiltOffsetR);
  delay(WALK_TILT_TIME_MS);

  servoRR.attach(RR_PIN, PULSE_MIN, PULSE_MAX);
  servoRR.writeMicroseconds(RR_STOP_US + rrSpeedUs);  // 반대 방향 회전
  delay(WALK_STEP_TIME_MS);
  footStop(servoRR, RR_PIN);

  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(WALK_NEUTRAL_HOLD_MS);
  delay(WALK_WAIT_MS);

  // 왼발 내딛기 (보폭 절반): 다리를 평평하게(90도) 둔 채 왼발(RL)을 반시계 방향으로 굴린다
  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(WALK_NEUTRAL_HOLD_MS);

  servoRL.attach(RL_PIN, PULSE_MIN, PULSE_MAX);
  servoRL.writeMicroseconds(RL_STOP_US - rlSpeedUs);  // 반시계 방향 (역회전)
  delay(WALK_STEP_TIME_MS / 2);
  footStop(servoRL, RL_PIN);

  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(WALK_NEUTRAL_HOLD_MS);
  delay(WALK_WAIT_MS);
}

// ---------------------------------------------------------------
// 오른쪽으로 돌며 걷기 (왼발 보폭은 그대로, 오른발 보폭은 절반)
// walkLeft()를 좌우 대칭으로 뒤집은 동작이다.
// ---------------------------------------------------------------
void walkRight() {
  // 처음 호출될 때 한 번만 서보와 시리얼을 초기화한다
  if (!s_walkStarted) {
    ottoInit(s_ylCenterCfg, s_yrCenterCfg);
    s_walkStarted = true;
  }

  int tiltOffsetL = s_tiltOffsetL;
  int rlSpeedUs = s_rlSpeedUs;
  int rrSpeedUs = s_rrSpeedUs;

  // 왼발 내딛기 (보폭 그대로): YR은 90도에 고정, YL만 반대 방향으로 기울여 왼발을 든다
  writeYR(NEUTRAL);
  writeYL(NEUTRAL - tiltOffsetL);
  delay(WALK_TILT_TIME_MS);

  servoRL.attach(RL_PIN, PULSE_MIN, PULSE_MAX);
  servoRL.writeMicroseconds(RL_STOP_US - rlSpeedUs);  // 반대 방향 회전
  delay(WALK_STEP_TIME_MS);
  footStop(servoRL, RL_PIN);

  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(WALK_NEUTRAL_HOLD_MS);
  delay(WALK_WAIT_MS);

  // 오른발 내딛기 (보폭 절반): 다리를 모두 90도에 둔 채 오른발(RR)을 굴린다
  servoRR.attach(RR_PIN, PULSE_MIN, PULSE_MAX);
  servoRR.writeMicroseconds(RR_STOP_US + rrSpeedUs);  // 반시계 방향 (walkLeft 왼발과 대칭)
  delay(WALK_STEP_TIME_MS / 2);
  footStop(servoRR, RR_PIN);

  writeYL(NEUTRAL);
  writeYR(NEUTRAL);
  delay(WALK_NEUTRAL_HOLD_MS);
  delay(WALK_WAIT_MS);
}

// 처음 호출될 때 한 번만 서보와 시리얼을 준비하고 발 서보를 붙인다. (발 동작 함수들이 공통으로 쓴다)
static void ensureFeetReady() {
  if (!s_walkStarted) {
    ottoInit(s_ylCenterCfg, s_yrCenterCfg);
    s_walkStarted = true;
  }
  if (!s_slideAttached) {
    servoRL.detach();   // 이미 붙어 있을 수 있으니 먼저 뗀 뒤 다시 붙인다
    servoRL.attach(RL_PIN, PULSE_MIN, PULSE_MAX);
    servoRR.detach();
    servoRR.attach(RR_PIN, PULSE_MIN, PULSE_MAX);
    servoRL.writeMicroseconds(RL_STOP_US);   // 붙인 직후 두 발을 정지 신호로 맞춘다
    servoRR.writeMicroseconds(RR_STOP_US);
    s_slideAttached = true;
  }
}

// ---------------------------------------------------------------
// 좌우 교대로 이어서 걷기 (부드러운 동작, walk_zigzag와 같은 순서)
// 엉덩이는 여러 단계로 나눠서 옮기고, 발은 멈출 때도 서보를 붙여 둔다.
// ---------------------------------------------------------------
static int s_curYL = NEUTRAL;
static int s_curYR = NEUTRAL;
static const int SLIDE_RAMP_MS = 120;        // 엉덩이를 옮기는 시간(ms)
static const int SLIDE_RAMP_STEPS = 12;      // 엉덩이를 나눠서 옮기는 단계 수
static const int SLIDE_ROLL_MS = 200;        // 발을 크게 굴리는 시간(ms)
static const int SLIDE_HALF_ROLL_MS = 100;   // 발을 짧게 굴리는 시간(ms)

// 엉덩이를 목표 각도까지 여러 단계로 나눠 옮긴다
static void moveHipsSmooth(int toYL, int toYR, int ms) {
  for (int i = 1; i <= SLIDE_RAMP_STEPS; i++) {
    writeYL(s_curYL + (toYL - s_curYL) * i / SLIDE_RAMP_STEPS);
    writeYR(s_curYR + (toYR - s_curYR) * i / SLIDE_RAMP_STEPS);
    delay(ms / SLIDE_RAMP_STEPS);
  }
  s_curYL = toYL;
  s_curYR = toYR;
}

// 발을 us 속도로 ms 동안 굴린 뒤 정지값으로 되돌린다 (서보는 붙인 채로 둔다)
static void rollHeld(OttoServo &servo, int stopUs, int us, int ms) {
  servo.writeMicroseconds(us);
  delay(ms);
  servo.writeMicroseconds(stopUs);
}

void slideFront() {
  ensureFeetReady();  // 처음 한 번 서보를 준비한다

  int tiltR = SLIDE_TILT_R;
  int tiltL = SLIDE_TILT_L;
  int rlSpeedUs = s_rlSpeedUs;
  int rrSpeedUs = s_rrSpeedUs;

  // 왼쪽 걸음: YR을 기울이고 RR을 굴린다 -> 평평하게 돌아와 RL을 짧게 굴린다
  moveHipsSmooth(NEUTRAL, NEUTRAL + tiltR, SLIDE_RAMP_MS);
  rollHeld(servoRR, RR_STOP_US, RR_STOP_US + rrSpeedUs, SLIDE_ROLL_MS);
  moveHipsSmooth(NEUTRAL, NEUTRAL, SLIDE_RAMP_MS);
  rollHeld(servoRL, RL_STOP_US, RL_STOP_US - rlSpeedUs, SLIDE_HALF_ROLL_MS);

  // 오른쪽 걸음: YL을 기울이고 RL을 굴린다 -> 평평하게 돌아와 RR을 짧게 굴린다
  moveHipsSmooth(NEUTRAL - tiltL, NEUTRAL, SLIDE_RAMP_MS);
  rollHeld(servoRL, RL_STOP_US, RL_STOP_US - rlSpeedUs, SLIDE_ROLL_MS);
  moveHipsSmooth(NEUTRAL, NEUTRAL, SLIDE_RAMP_MS);
  rollHeld(servoRR, RR_STOP_US, RR_STOP_US + rrSpeedUs, SLIDE_HALF_ROLL_MS);
}

// ---------------------------------------------------------------
// 좌우 교대로 부드럽게 뒤로 움직이기 (slideFront와 같은 순서, 발 굴리는 방향만 반대)
// ---------------------------------------------------------------
void slideBack() {
  ensureFeetReady();  // 처음 한 번 서보를 준비한다

  int tiltR = SLIDE_TILT_R;
  int tiltL = SLIDE_TILT_L;
  int rlSpeedUs = s_rlSpeedUs;
  int rrSpeedUs = s_rrSpeedUs;

  // 왼쪽 걸음: YR을 기울이고 RR을 반대로 굴린다 -> 평평하게 돌아와 RL을 짧게 반대로 굴린다
  moveHipsSmooth(NEUTRAL, NEUTRAL + tiltR, SLIDE_RAMP_MS);
  rollHeld(servoRR, RR_STOP_US, RR_STOP_US - rrSpeedUs, SLIDE_ROLL_MS);
  moveHipsSmooth(NEUTRAL, NEUTRAL, SLIDE_RAMP_MS);
  rollHeld(servoRL, RL_STOP_US, RL_STOP_US + rlSpeedUs, SLIDE_HALF_ROLL_MS);

  // 오른쪽 걸음: YL을 기울이고 RL을 반대로 굴린다 -> 평평하게 돌아와 RR을 짧게 반대로 굴린다
  moveHipsSmooth(NEUTRAL - tiltL, NEUTRAL, SLIDE_RAMP_MS);
  rollHeld(servoRL, RL_STOP_US, RL_STOP_US + rlSpeedUs, SLIDE_ROLL_MS);
  moveHipsSmooth(NEUTRAL, NEUTRAL, SLIDE_RAMP_MS);
  rollHeld(servoRR, RR_STOP_US, RR_STOP_US - rrSpeedUs, SLIDE_HALF_ROLL_MS);
}

// ---------------------------------------------------------------
// 좌우 교대로 부드럽게 움직이기 (slideFront를 좌우 대칭으로 뒤집은 동작)
// ---------------------------------------------------------------
void slideRight() {
  ensureFeetReady();  // 처음 한 번 서보를 준비한다

  int tiltR = SLIDE_TILT_R;
  int tiltL = SLIDE_TILT_L;
  int rlSpeedUs = s_rlSpeedUs;
  int rrSpeedUs = s_rrSpeedUs;

  // 왼쪽 발 걸음: YL을 기울이고 RL을 굴린다 -> 평평하게 돌아와 RR을 짧게 굴린다
  moveHipsSmooth(NEUTRAL - tiltL, NEUTRAL, SLIDE_RAMP_MS);
  rollHeld(servoRL, RL_STOP_US, RL_STOP_US - rlSpeedUs, SLIDE_ROLL_MS);
  moveHipsSmooth(NEUTRAL, NEUTRAL, SLIDE_RAMP_MS);
  rollHeld(servoRR, RR_STOP_US, RR_STOP_US + rrSpeedUs, SLIDE_HALF_ROLL_MS);

  // 오른쪽 발 걸음: YR을 기울이고 RR을 굴린다 -> 평평하게 돌아와 RL을 짧게 굴린다
  moveHipsSmooth(NEUTRAL, NEUTRAL + tiltR, SLIDE_RAMP_MS);
  rollHeld(servoRR, RR_STOP_US, RR_STOP_US + rrSpeedUs, SLIDE_ROLL_MS);
  moveHipsSmooth(NEUTRAL, NEUTRAL, SLIDE_RAMP_MS);
  rollHeld(servoRL, RL_STOP_US, RL_STOP_US - rlSpeedUs, SLIDE_HALF_ROLL_MS);
}

// ---------------------------------------------------------------
// 두 발 동시에 반대 방향으로 돌기 (오른쪽 다리만 기울인다)
// 왼발 시계, 오른발 반시계 (slideRight를 좌우 대칭으로 뒤집은 동작)
// ---------------------------------------------------------------
void slideLeft() {
  ensureFeetReady();  // 처음 한 번 서보를 준비한다

  int tiltR = SLIDE_TILT_R;
  int rlSpeedUs = s_rlSpeedUs;
  int rrSpeedUs = s_rrSpeedUs;

  // 오른쪽 다리를 기울인 뒤(왼쪽 다리는 90도 유지), 두 발을 같은 시점에 굴린다
  moveHipsSmooth(NEUTRAL, NEUTRAL + tiltR, SLIDE_RAMP_MS);
  servoRL.writeMicroseconds(RL_STOP_US + rlSpeedUs);  // 왼발 시계
  servoRR.writeMicroseconds(RR_STOP_US + rrSpeedUs);  // 오른발 반시계
  delay(SLIDE_ROLL_MS);
  servoRL.writeMicroseconds(RL_STOP_US);
  servoRR.writeMicroseconds(RR_STOP_US);

  // 다리를 90도로 되돌린다
  moveHipsSmooth(NEUTRAL, NEUTRAL, SLIDE_RAMP_MS);
}

// ---------------------------------------------------------------
// 엉덩이 끝까지 돌린 뒤 발 굴리기 (rolling_front 동작)
// 순서: YR 0 / YL 180 으로 한 번 돌린 뒤, 발을 멈추지 않고 앞으로 계속 굴린다.
// 호출할 때마다 멈추지 않고 바로 돌아오므로, loop()에서 호출해도 회전이 이어진다.
// ---------------------------------------------------------------
void rollingFront(int velocity) {
  const int hipHoldMs = 1000;   // 엉덩이를 끝까지 움직인 뒤 머무는 시간

  ensureFeetReady();  // 처음 한 번 서보를 준비한다

  // 속도(velocity): 20~200(%). 100이 보정한 기본 속도다. 발 속도를 비율만큼 바꾼다.
  velocity = constrain(velocity, ROLL_VELOCITY_MIN, ROLL_VELOCITY_MAX);
  int rlSpeedUs = s_rlSpeedUs * velocity / 100;
  int rrSpeedUs = s_rrSpeedUs * velocity / 100;
  // 서보 신호 범위를 넘지 않게 하되, 왼발과 오른발의 비율(직진 보정)은 그대로 유지한다.
  int fastest = max(rlSpeedUs, rrSpeedUs);
  if (fastest > ROLL_MAX_OFFSET_US) {
    rlSpeedUs = rlSpeedUs * ROLL_MAX_OFFSET_US / fastest;
    rrSpeedUs = rrSpeedUs * ROLL_MAX_OFFSET_US / fastest;
  }

  // 엉덩이는 처음 한 번만 끝까지 움직인다: YR 0도, YL 180도
  if (!s_hipsAtEnds) {
    writeYR(0);
    writeYL(180);
    delay(hipHoldMs);
    s_curYL = 180;
    s_curYR = 0;
    s_hipsAtEnds = true;
  }

  // 발은 멈추지 않고 계속 앞으로 굴린다 (왼발 +, 오른발 -). 정지 명령을 주지 않는다.
  servoRL.writeMicroseconds(RL_STOP_US + rlSpeedUs);
  servoRR.writeMicroseconds(RR_STOP_US - rrSpeedUs);
}

// 기본 속도(100)로 앞으로 굴리기
void rollingFront() {
  rollingFront(100);
}

// ---------------------------------------------------------------
// 엉덩이 끝까지 돌린 뒤 발을 뒤로 계속 굴리기 (rollingFront의 반대 방향)
// 순서: YR 0 / YL 180 으로 한 번 돌린 뒤, 발을 멈추지 않고 뒤로 계속 굴린다.
// ---------------------------------------------------------------
void rollingBack(int velocity) {
  const int hipHoldMs = 1000;        // 엉덩이를 끝까지 움직인 뒤 머무는 시간

  ensureFeetReady();  // 처음 한 번 서보를 준비한다

  // 속도(velocity): 20~200(%). 100이 보정한 기본 속도다. 발 속도를 비율만큼 바꾼다. (rollingFront(velocity)와 같은 규칙)
  velocity = constrain(velocity, ROLL_VELOCITY_MIN, ROLL_VELOCITY_MAX);
  int rlSpeedUs = s_rlSpeedUs * velocity / 100;
  int rrSpeedUs = s_rrSpeedUs * velocity / 100;
  // 서보 신호 범위를 넘지 않게 하되, 왼발과 오른발의 비율(직진 보정)은 그대로 유지한다.
  int fastest = max(rlSpeedUs, rrSpeedUs);
  if (fastest > ROLL_MAX_OFFSET_US) {
    rlSpeedUs = rlSpeedUs * ROLL_MAX_OFFSET_US / fastest;
    rrSpeedUs = rrSpeedUs * ROLL_MAX_OFFSET_US / fastest;
  }

  // 엉덩이는 처음 한 번만 끝까지 움직인다: YR 0도, YL 180도
  if (!s_hipsAtEnds) {
    writeYR(0);
    writeYL(180);
    delay(hipHoldMs);
    s_curYL = 180;
    s_curYR = 0;
    s_hipsAtEnds = true;
  }

  // 발은 멈추지 않고 계속 뒤로 굴린다 (왼발 -, 오른발 +). 정지 명령을 주지 않는다.
  servoRL.writeMicroseconds(RL_STOP_US - rlSpeedUs);
  servoRR.writeMicroseconds(RR_STOP_US + rrSpeedUs);
}

// 기본 속도(100)로 뒤로 굴리기
void rollingBack() {
  rollingBack(100);
}

// ---------------------------------------------------------------
// 엉덩이 끝까지 돌린 뒤 제자리에서 오른쪽으로 계속 돌기
// 왼발은 앞으로(+), 오른발은 뒤로(+) 굴려서 몸이 오른쪽으로 돈다.
// ---------------------------------------------------------------
void rollingRight() {
  const int hipHoldMs = 1000;          // 엉덩이를 끝까지 움직인 뒤 머무는 시간

  ensureFeetReady();  // 처음 한 번 서보를 준비한다

  int rlSpeedUs = s_rlSpeedUs;
  int rrSpeedUs = s_rrSpeedUs;

  // 엉덩이는 처음 한 번만 끝까지 움직인다: YR 0도, YL 180도
  if (!s_hipsAtEnds) {
    writeYR(0);
    writeYL(180);
    delay(hipHoldMs);
    s_curYL = 180;
    s_curYR = 0;
    s_hipsAtEnds = true;
  }

  // 발은 멈추지 않고 계속 돈다: 왼발 앞으로(+), 오른발 뒤로(+). 정지 명령을 주지 않는다.
  servoRL.writeMicroseconds(RL_STOP_US + rlSpeedUs);
  servoRR.writeMicroseconds(RR_STOP_US + rrSpeedUs);
}

// ---------------------------------------------------------------
// 엉덩이 끝까지 돌린 뒤 제자리에서 왼쪽으로 계속 돌기 (rollingRight의 반대 방향)
// 왼발은 뒤로(-), 오른발은 앞으로(-) 굴려서 몸이 왼쪽으로 돈다.
// ---------------------------------------------------------------
void rollingLeft() {
  const int hipHoldMs = 1000;         // 엉덩이를 끝까지 움직인 뒤 머무는 시간

  ensureFeetReady();  // 처음 한 번 서보를 준비한다

  int rlSpeedUs = s_rlSpeedUs;
  int rrSpeedUs = s_rrSpeedUs;

  // 엉덩이는 처음 한 번만 끝까지 움직인다: YR 0도, YL 180도
  if (!s_hipsAtEnds) {
    writeYR(0);
    writeYL(180);
    delay(hipHoldMs);
    s_curYL = 180;
    s_curYR = 0;
    s_hipsAtEnds = true;
  }

  // 발은 멈추지 않고 계속 돈다: 왼발 뒤로(-), 오른발 앞으로(-). 정지 명령을 주지 않는다.
  servoRL.writeMicroseconds(RL_STOP_US - rlSpeedUs);
  servoRR.writeMicroseconds(RR_STOP_US - rrSpeedUs);
}

// ---------------------------------------------------------------
// 오른발 지지, 왼발 들고 오른쪽으로 돌기 (춤 동작)
// 순서: 왼쪽 엉덩이(YL)를 기울여 왼발을 든다 -> 땅에 디딘 오른발(RR)을 굴려 오른쪽으로 돈다 -> 왼발을 내려놓는다.
// 오른쪽 엉덩이(YR)는 90도에 고정하고, 왼발(RL)은 들린 채로 둔다.
// ---------------------------------------------------------------
void danceRightRightTurn() {
  const int rampMs = 120;      // 왼쪽 다리를 옮기는 시간
  const int rampSteps = 12;    // 다리를 나눠서 옮기는 단계 수
  const int rollMs = 200;      // 왼발을 굴리는 시간
  const int pauseMs = 300;     // 왼발을 내려놓은 뒤 멈추는 시간
  static int s_danceYL = NEUTRAL;  // 현재 왼쪽 다리 각도
  static int s_danceYR = NEUTRAL;  // 현재 오른쪽 다리 각도

  ensureFeetReady();  // 처음 한 번 서보를 준비한다

  int rrSpeedUs = s_rrSpeedUs;

  // 0) 오른쪽 다리를 기울여 몸무게를 오른발 위로 옮긴다 (한 발로 서기 위한 준비)
  int shiftTargetR = NEUTRAL - s_tiltOffsetR;
  for (int i = 1; i <= rampSteps; i++) {
    writeYR(s_danceYR + (shiftTargetR - s_danceYR) * i / rampSteps);
    delay(rampMs / rampSteps);
  }
  s_danceYR = shiftTargetR;

  // 1) 왼쪽 다리를 기울여 왼발을 든다
  int liftTarget = NEUTRAL - s_tiltOffsetL;
  for (int i = 1; i <= rampSteps; i++) {
    writeYL(s_danceYL + (liftTarget - s_danceYL) * i / rampSteps);
    delay(rampMs / rampSteps);
  }
  s_danceYL = liftTarget;

  // 2) 땅에 디딘 오른발을 굴려 오른쪽으로 돈다 (왼발은 들린 채로 둔다)
  servoRR.writeMicroseconds(RR_STOP_US + rrSpeedUs);
  delay(rollMs);
  servoRR.writeMicroseconds(RR_STOP_US);

  // 3) 왼쪽 다리를 90도로 되돌려 왼발을 내려놓는다
  for (int i = 1; i <= rampSteps; i++) {
    writeYL(s_danceYL + (NEUTRAL - s_danceYL) * i / rampSteps);
    delay(rampMs / rampSteps);
  }
  s_danceYL = NEUTRAL;
  s_curYL = NEUTRAL;

  // 4) 오른쪽 다리를 90도로 되돌린다
  for (int i = 1; i <= rampSteps; i++) {
    writeYR(s_danceYR + (NEUTRAL - s_danceYR) * i / rampSteps);
    delay(rampMs / rampSteps);
  }
  s_danceYR = NEUTRAL;
  s_curYR = NEUTRAL;
  delay(pauseMs);
}

// ---------------------------------------------------------------
// 발(RL, RR) 속도 지정 - 스케치는 서보 객체나 attach를 다루지 않는다
// rr: 오른발 속도, rl: 왼발 속도. 양수 = 앞으로, 음수 = 뒤로, 0 = 멈춤. (-400 ~ 400)
// ---------------------------------------------------------------
void ottoRollFeet(int rr, int rl) {
  ensureFeetReady();  // 처음 한 번 서보를 준비한다

  rr = constrain(rr, -400, 400);
  rl = constrain(rl, -400, 400);
  servoRR.writeMicroseconds(RR_STOP_US - rr);   // 오른발: 양수 = 앞으로 (rollingFront와 같은 방향)
  servoRL.writeMicroseconds(RL_STOP_US + rl);   // 왼발: 양수 = 앞으로
}

// ---------------------------------------------------------------
// 엉덩이 각도 지정 (오른쪽 yr, 왼쪽 yl). 발은 움직이지 않는다.
// ---------------------------------------------------------------
void moveHip(int yr, int yl) {
  // 처음 호출될 때 한 번만 서보와 시리얼을 초기화한다 (엉덩이는 가운데로 맞춰진다)
  if (!s_walkStarted) {
    ottoInit(s_ylCenterCfg, s_yrCenterCfg);
    s_walkStarted = true;
  }
  yr = constrain(yr, 0, 180);
  yl = constrain(yl, 0, 180);
  writeYR(yr);
  writeYL(yl);
}

// ---------------------------------------------------------------
// 발 하나씩 제어 (오른발 RR만, 왼발 RL만). 다른 발은 건드리지 않는다.
// 양수 = 앞으로, 음수 = 뒤로, 0 = 멈춤. 값은 -400 ~ 400으로 제한한다.
// ---------------------------------------------------------------

void rollRightFoot(int speed) {
  ensureFeetReady();
  speed = constrain(speed, -400, 400);
  servoRR.writeMicroseconds(RR_STOP_US - speed);   // 오른발: 양수 = 앞으로 (ottoRollFeet과 같은 방향)
}

void rollLeftFoot(int speed) {
  ensureFeetReady();
  speed = constrain(speed, -400, 400);
  servoRL.writeMicroseconds(RL_STOP_US + speed);   // 왼발: 양수 = 앞으로
}
