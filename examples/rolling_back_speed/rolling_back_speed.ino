#include <OttoI2R.h>

// 속도를 바꿔 가며 뒤로 굴러가기 (쉬운 버전)
// rollingBack(숫자)는 그 숫자의 속도로 뒤로 굴러가요. 100이 보통 속도, 클수록 빨라요. (40 ~ 200)
// 한 번 부르면 로봇은 계속 굴러가요. 속도를 바꾸려면 다른 숫자로 다시 부르면 돼요.
// 느린 속도에서 빠른 속도까지 올라갔다가, 다시 느린 속도까지 내려와요.

void setup() {
  ottoInit();   // 로봇을 준비해요
}

void loop() {
  // 40, 60, 80 ... 200 : 점점 빨라져요
  for (int speed = 40; speed <= 200; speed = speed + 20) {
    Serial.println(speed);    // 지금 속도를 보여 줘요
    rollingBack(speed);       // 이 속도로 뒤로 굴러가요
    delay(2000);              // 2초 동안 그대로 굴러가요
  }

  // 200, 180, 160 ... 40 : 점점 느려져요
  for (int speed = 200; speed >= 40; speed = speed - 20) {
    Serial.println(speed);
    rollingBack(speed);
    delay(2000);
  }
}
