#pragma once
#include <Arduino.h>

// Otto 로봇(YL, YR, RL, RR 서보 4개) 공통 라이브러리.
// 핀 번호, 서보 초기화, 각도 보정처럼 "로봇마다 똑같은 부분"을 여기 모아 뒀다.
// 로봇마다 다른 보정값은 웹의 [로봇 맞추기]에서 맞춰 보드에 저장하고, 스케치는 ottoInit()으로 불러 쓴다.

// 서보 초기화. 보드에 저장된 보정값이 있으면 불러온 뒤(웹의 [로봇 맞추기]에서 저장한 값),
// 없으면 기본값으로 서보를 초기화한다. (Serial.begin(115200)도 같이 한다)
// 스케치는 setup()에서 ottoInit(); 한 줄이면 된다. 여러 번 불러도 처음 한 번만 동작한다.
// 기본값(YL 1250, YR 1450, 기울기 20/23, 속도 450/550)은 한 대의 로봇에서 실측한 값이라,
// 아직 저장하지 않은 보드에서는 로봇에 따라 안 맞을 수 있다. 먼저 [로봇 맞추기]로 맞춰 저장하세요.
void ottoInit();

// 서보 초기화. 다리(YL, YR) 중립점을 직접 넘긴다. (저장된 값은 불러오지 않는다)
// 동작 함수(walkFront 등)는 처음 호출될 때 알아서 이 함수를 부르므로, 스케치에서 직접 부를 필요는 없다.
void ottoInit(int ylPulseCenter, int yrPulseCenter);

// 보정된 각도(0~180)로 엉덩이 서보를 움직인다.
void writeYL(int angle);
void writeYR(int angle);

// 360도 회전 테스트: 0도/180도 -> 전진 -> 정지 -> 후진 -> 정지 -> 90도
// 두 바퀴(RL, RR)가 동시에 같은 속도로 돌 때 똑바로 가는지 확인한다.
// 속도 값(RL_SPEED_US, RR_SPEED_US)은 보정값(웹 [로봇 맞추기]에서 저장한 값, 없으면 기본값)을 쓴다.
void ottoCalibrationRolling();

// 걷기 테스트: 오른발 -> 왼발 번갈아 한 걸음씩 내딛는다. loop()에서 반복 호출한다.
// 기울기 값(TILT_OFFSET_R/L)은 보정값(웹 [로봇 맞추기]에서 저장한 값, 없으면 기본값)을 쓴다.
// 걷는 속도·보폭(타이밍)은 이 라이브러리 안의 고정값을 쓴다. OttoI2R.cpp에서 조절한다.
void ottoCalibrationWalk();

// 보정값을 코드에서 직접 넣는다. 서보는 움직이지 않고 값만 저장한다.
// 서보가 이미 켜져 있으면 다리 중심값도 바로 바뀐다. (다음에 다리를 움직일 때부터 적용된다)
void ottoSetCalibration(int ylPulseCenter, int yrPulseCenter, int tiltOffsetR, int tiltOffsetL, int rlSpeedUs, int rrSpeedUs);

// ── 보정값을 보드(ESP32)의 NVS 에 저장해 두고 불러오는 함수들 ──────────────────
// 로봇을 한 번 보정해서 저장해 두면, 그 보드에서 만드는 모든 스케치가 같은 보정값을 쓴다.
// 사용법:  setup() 에서 ottoInit(); 한 줄이면 된다. (불러오기만 하고 서보는 움직이지 않으려면 ottoLoadCalibration())
//   (보정값은 기본값으로 시작하므로, 저장된 값이 있을 때만 그 값으로 바뀐다)
struct OttoCalibration {
  int ylCenter;  // 왼쪽 다리가 수직(90도)일 때의 펄스폭(us)   1000~1900
  int yrCenter;  // 오른쪽 다리                                1000~1900
  int tiltR;     // 걷기: 오른발을 들 때 기울이는 각도           0~45
  int tiltL;     // 걷기: 왼발을 들 때                           0~45
  int rlSpeed;   // 왼발 굴림 속도(us)                           0~600
  int rrSpeed;   // 오른발 굴림 속도(us)                         0~600
};

// NVS 에 저장된 값을 읽는다. 저장된 값이 있고 범위가 맞으면 적용하고 true, 아니면 현재 값을 그대로 두고 false.
bool ottoLoadCalibration();
// 지금 보정값(ottoSetCalibration 으로 넣은 값)을 NVS 에 저장한다. 성공하면 true.
bool ottoSaveCalibration();
// NVS 에 저장된 보정값을 지운다. (현재 값은 그대로)
void ottoClearCalibration();
// NVS 에 저장된 값이 있는지
bool ottoHasSavedCalibration();
// 지금 적용 중인 보정값을 읽는다.
void ottoGetCalibration(OttoCalibration &out);
// 범위를 벗어난 값은 자동으로 범위 안으로 맞춘다.
void ottoSetHipCenter(int ylPulseCenter, int yrPulseCenter);  // 다리 중심값만 바꾼다 (바로 적용)
void ottoSetTilt(int tiltOffsetR, int tiltOffsetL);           // 기울기만 바꾼다
void ottoSetSpeed(int rlSpeedUs, int rrSpeedUs);              // 발 속도만 바꾼다

// 앞으로 직진 걷기: 오른발 -> 왼발 번갈아 앞으로 걷는다. loop()에서 반복 호출한다.
// 기울기와 속도는 ottoSetCalibration()으로 등록한 보정값을 쓴다.
void walkFront();

// 속도를 정해서 앞으로 걷는다. velocity는 20~200(%)이고, 100이 보정한 기본 속도다. loop()에서 반복 호출한다.
//  - 100 이하: 50이면 절반 속도. 20 미만은 20으로 맞춘다.
//  - 100 초과: 보폭과 발 속도는 그대로 두고 기다리는 시간만 줄여 걸음 주기를 빠르게 한다. 200 초과는 200으로 맞춘다.
void walkFront(int velocity);

// 뒤로 걷기: 오른발 -> 왼발 번갈아 뒤로 걷는다. loop()에서 반복 호출한다.
// 기울기와 속도는 보정값(웹 [로봇 맞추기]에서 저장한 값, 없으면 기본값)을 쓴다.
void walkBack();

// 속도를 정해서 뒤로 걷는다. velocity는 20~200(%)이고, 100이 보정한 기본 속도다. (walkFront(velocity)와 같은 규칙)
//  - 100 이하: 50이면 절반 속도. 20 미만은 20으로 맞춘다.
//  - 100 초과: 보폭과 발 속도는 그대로 두고 기다리는 시간만 줄여 걸음 주기를 빠르게 한다. 200 초과는 200으로 맞춘다.
void walkBack(int velocity);

// 왼쪽으로 돌며 걷기: 오른발은 그대로, 왼발 보폭을 절반으로 줄인다. loop()에서 반복 호출한다.
// 기울기와 속도는 보정값(웹 [로봇 맞추기]에서 저장한 값, 없으면 기본값)을 쓴다.
void walkLeft();

// 오른쪽으로 돌며 걷기: 왼발은 그대로, 오른발 보폭을 절반으로 줄인다. loop()에서 반복 호출한다.
// 기울기와 속도는 보정값(웹 [로봇 맞추기]에서 저장한 값, 없으면 기본값)을 쓴다.
void walkRight();

// 미끄러지듯 전진: YL, YR은 90도에 고정하고 RL, RR을 함께 앞으로 굴린다. loop()에서 반복 호출한다.
// 속도는 보정값(웹 [로봇 맞추기]에서 저장한 값, 없으면 기본값)을 쓴다.
void slideFront();

// 좌우 교대로 부드럽게 뒤로 움직인다: slideFront()와 같은 순서, 발 굴리는 방향만 반대.
// 속도와 기울기는 보정값(웹 [로봇 맞추기]에서 저장한 값, 없으면 기본값)을 쓴다.
void slideBack();

// 좌우 교대로 부드럽게 움직이되, slideFront()를 좌우 대칭으로 뒤집은 동작.
// 먼저 왼발(RL)을 크게 굴리고, 나중에 오른발(RR)을 짧게 굴린다.
void slideRight();

// 두 발을 같은 시점에 반대 방향으로 굴린다. 오른쪽 다리(YR)만 기울인다.
// 왼발은 시계, 오른발은 반시계 방향이다. 속도와 기울기는 보정값(웹 [로봇 맞추기]에서 저장한 값, 없으면 기본값)을 쓴다.
void slideLeft();

// 엉덩이를 한 번 끝까지 돌린 뒤(YR 0도, YL 180도), 발을 멈추지 않고 앞으로 계속 굴린다.
// 호출할 때마다 바로 돌아오므로 loop()에서 반복 호출해도 회전이 끊기지 않는다.
// 멈추려면 보드를 리셋하거나 전원을 끈다.
void rollingFront();

// 속도를 정해서 앞으로 계속 굴린다. velocity는 20~200(%)이고, 100이 보정한 기본 속도다.
// 왼발과 오른발에 같은 비율을 적용해서 직진 보정이 유지된다. 서보가 낼 수 있는 한계를 넘는 값은 한계에서 멈춘다.
// 호출하면 바로 돌아오고 발은 계속 돈다. 속도를 바꾸려면 다른 숫자로 다시 부른다.
void rollingFront(int velocity);

// 엉덩이를 한 번 끝까지 돌린 뒤(YR 0도, YL 180도), 발을 멈추지 않고 뒤로 계속 굴린다.
// rollingFront()와 같은 방식이고, 발 굴리는 방향만 반대다. loop()에서 반복 호출한다.
void rollingBack();

// 속도를 정해서 뒤로 계속 굴린다. velocity는 20~200(%)이고, 100이 보정한 기본 속도다. (rollingFront(velocity)와 같은 규칙)
// 왼발과 오른발에 같은 비율을 적용해서 직진 보정이 유지된다. 서보가 낼 수 있는 한계를 넘는 값은 한계에서 멈춘다.
// 호출하면 바로 돌아오고 발은 계속 돈다. 속도를 바꾸려면 다른 숫자로 다시 부른다.
void rollingBack(int velocity);

// 엉덩이를 한 번 끝까지 돌린 뒤(YR 0도, YL 180도), 제자리에서 오른쪽으로 계속 돈다.
// 왼발은 앞으로, 오른발은 뒤로 멈추지 않고 굴린다. loop()에서 반복 호출한다.
void rollingRight();

// 엉덩이를 한 번 끝까지 돌린 뒤(YR 0도, YL 180도), 제자리에서 왼쪽으로 계속 돈다.
// 왼발은 뒤로, 오른발은 앞으로 멈추지 않고 굴린다. loop()에서 반복 호출한다.
void rollingLeft();

// 오른발은 땅에 디디고 왼발을 들어 올린 뒤, 왼발을 앞으로 굴려 오른쪽으로 돈다.
// 한 번 호출하면 한 동작(들기 -> 굴리기 -> 내려놓기)을 수행한다. loop()에서 반복 호출한다.
void danceRightRightTurn();

// 두 발의 속도를 정한다. rr: 오른발, rl: 왼발. 양수 = 앞으로, 음수 = 뒤로, 0 = 멈춤. (-400 ~ 400)
// 서보 연결(attach)은 처음 호출할 때 라이브러리가 알아서 한다.
void ottoRollFeet(int rr, int rl);

// 오른쪽 엉덩이(yr)와 왼쪽 엉덩이(yl)를 각도로 움직인다. 0~180, 90이 가운데다.
// 처음 호출할 때 라이브러리가 서보를 자동으로 붙이고 가운데(90도)로 맞춘다. 발은 움직이지 않는다.
void moveHip(int yr, int yl);

// 오른발(RR)만 속도로 굴린다. 양수 = 앞으로, 음수 = 뒤로, 0 = 멈춤. (-400 ~ 400)
// 왼발은 건드리지 않는다. 서보 연결은 처음 호출할 때 자동으로 한다.
void rollRightFoot(int speed);

// 왼발(RL)만 속도로 굴린다. 양수 = 앞으로, 음수 = 뒤로, 0 = 멈춤. (-400 ~ 400)
// 오른발은 건드리지 않는다. 서보 연결은 처음 호출할 때 자동으로 한다.
void rollLeftFoot(int speed);
