#include <OttoI2R.h>

// 발 하나씩 움직이는 예제 (앞으로, 뒤로 모두 포함).
// 1) 오른발만 앞으로 2초 -> 멈춤 1초
// 2) 오른발만 뒤로 2초   -> 멈춤 1초
// 3) 왼발만 앞으로 2초   -> 멈춤 1초
// 4) 왼발만 뒤로 2초     -> 멈춤 1초
// 엉덩이(YL, YR)는 움직이지 않는다. 이 순서를 반복한다.

// 발 속도는 아래에서 숫자로 직접 정한다. 서보는 ottoInit()에서 초기화한다.
void setup() {
  ottoInit();
}

void loop() {

  rollRightFoot(400);     // 오른발 앞으로
  delay(2000);
  rollRightFoot(0);      // 오른발 멈춤
  delay(1000);

  rollRightFoot(-80);    // 오른발 뒤로 (역회전)
  delay(2000);
  rollRightFoot(0);      // 오른발 멈춤
  delay(1000);

  rollLeftFoot(80);      // 왼발 앞으로
  delay(2000);
  rollLeftFoot(0);       // 왼발 멈춤
  delay(1000);

  rollLeftFoot(-80);     // 왼발 뒤로 (역회전)
  delay(2000);
  rollLeftFoot(0);       // 왼발 멈춤
  delay(1000);
}
