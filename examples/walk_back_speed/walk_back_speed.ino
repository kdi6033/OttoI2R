#include <OttoI2R.h>

// 속도를 바꿔 가며 뒤로 걷기 (쉬운 버전)
// walkBack(숫자)는 그 숫자의 속도로 뒤로 걸어요. 100이 보통 속도, 클수록 빨라요. (40 ~ 200)
// 느린 속도에서 빠른 속도까지 올라갔다가, 다시 느린 속도까지 내려와요.

void setup() {
  ottoInit();   // 로봇을 준비해요
}

void loop() {
  // 40, 60, 80 ... 200 : 점점 빨라져요
  for (int speed = 40; speed <= 200; speed = speed + 20) {
    Serial.println(speed);   // 지금 속도를 보여 줘요
    walkBack(speed);         // 한 걸음 (오른발, 왼발)
    walkBack(speed);         // 한 걸음 더
  }

  // 200, 180, 160 ... 40 : 점점 느려져요
  for (int speed = 200; speed >= 40; speed = speed - 20) {
    Serial.println(speed);
    walkBack(speed);
    walkBack(speed);
  }
}
