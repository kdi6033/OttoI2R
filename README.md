# OttoI2R

Otto 로봇(YL, YR, RL, RR 서보 4개)을 위한 Arduino 라이브러리입니다. 보드는 [i2r-05](https://github.com/kdi6033/i2r-05)(ESP32-S3)입니다.
핀 번호, 서보 초기화, 각도 보정, 걷기와 굴리기 같은 기본 동작이 들어 있습니다.

- 제작 가이드와 통신 규격: https://github.com/kdi6033/otto

## 설치

Arduino IDE에서 **스케치 → 라이브러리 포함하기 → .ZIP 라이브러리 추가**로 `OttoI2R.zip`을 선택합니다.

- **추가로 설치할 라이브러리는 없습니다.** 서보는 ESP32 기본 PWM(LEDC)으로 직접 구동합니다.
- **ESP32 보드 패키지 3.0 이상**이 필요합니다. (보드 매니저에서 `esp32 by Espressif Systems` 3.x 설치)

## 사용

```cpp
#include <OttoI2R.h>

void setup() {
  ottoInit();       // 저장된 보정값을 불러오고 서보를 초기화한다
}

void loop() {
  walkFront();      // 앞으로 걷기
}
```

예제는 **파일 → 예제 → OttoI2R**에 있습니다. (walk, walk_front_speed, walk_back_speed, slide, rolling, rolling_front_speed, rolling_back_speed, hip_move, roll_one_foot)

속도를 정해서 움직이려면 `walkFront(속도)`, `walkBack(속도)`, `rollingFront(속도)`, `rollingBack(속도)`를 쓰세요. 속도는 20~200이고 100이 보통 속도입니다. (예: `walkFront(150);`)

## 보정

로봇마다 조립 상태가 달라서 보정이 필요합니다. 웹의 **OTTO 조종 → 로봇 맞추기**에서 맞추고 **저장**하면 보드에 기록되고,
`ottoInit()`이 그 값을 불러옵니다.

> 아직 저장하지 않은 보드는 기본값(YL 1250, YR 1450, 기울기 20/23, 속도 450/550)으로 움직입니다.
> 이 값은 한 대의 로봇에서 실측한 값이라 로봇에 따라 안 맞을 수 있으니, 먼저 [로봇 맞추기]를 하세요.

## 주요 함수

| 구분 | 함수 |
|---|---|
| 초기화 | `ottoInit()` |
| 걷기 | `walkFront()`, `walkFront(속도)`, `walkBack()`, `walkBack(속도)`, `walkLeft()`, `walkRight()` |
| 미끄러지기 | `slideFront()`, `slideBack()`, `slideLeft()`, `slideRight()` |
| 굴리기 | `rollingFront()`, `rollingFront(속도)`, `rollingBack()`, `rollingBack(속도)`, `rollingLeft()`, `rollingRight()` |
| 자세와 발 | `moveHip(yr, yl)`, `rollRightFoot(속도)`, `rollLeftFoot(속도)`, `ottoRollFeet(rr, rl)` |
| 춤 | `danceRightRightTurn()` |
| 보정값 | `ottoLoadCalibration()`, `ottoSaveCalibration()`, `ottoClearCalibration()`, `ottoHasSavedCalibration()`, `ottoGetCalibration()`, `ottoSetCalibration()`, `ottoSetHipCenter()`, `ottoSetTilt()`, `ottoSetSpeed()` |

- `ottoInit()`: 서보를 움직이지 않고 보정값만 불러오려면 `ottoLoadCalibration()`을 쓰세요.
- `ottoInit(yl, yr)`: 다리 중심값을 직접 넘기는 방식입니다. (저장된 값은 불러오지 않음)
- `walkFront(속도)`, `walkBack(속도)`, `rollingFront(속도)`, `rollingBack(속도)`: 속도는 20~200(%)입니다. 100이 보정한 기본 속도이고, 100을 넘으면 보폭은 그대로 두고 기다리는 시간만 줄여서 걸음을 빠르게 합니다.
- 걷는 속도와 보폭 같은 고정값은 `src/OttoI2R.cpp`에 있습니다.

## 버전

1.0.0: 첫 배포.
