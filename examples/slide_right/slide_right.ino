#include <OttoI2R.h>

// 오른쪽으로 미끄러지듯 움직이기: slideFront()를 좌우 대칭으로 뒤집은 동작이다.
// 동작은 OttoI2R 라이브러리의 slideRight()에 들어 있다.
// 보정값은 웹의 [로봇 맞추기]에서 맞춰 보드에 저장한 값을 쓴다.
// 아직 저장하지 않은 보드는 기본값으로 움직인다. 로봇에 따라 안 맞을 수 있으니 먼저 [로봇 맞추기]를 하세요.

void setup() {
  ottoInit();
}

void loop() {
  slideRight();
}
