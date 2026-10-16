#include <OttoI2R.h>

// 좌우 교대로 부드럽게 뒤로 움직인다 (slide_front와 같은 순서, 발 굴리는 방향만 반대).
// 동작은 OttoI2R 라이브러리의 slideBack()에 들어 있다.
// 보정값은 웹의 [로봇 맞추기]에서 맞춰 보드에 저장한 값을 쓴다.
// 아직 저장하지 않은 보드는 기본값으로 움직인다. 로봇에 따라 안 맞을 수 있으니 먼저 [로봇 맞추기]를 하세요.

void setup() {
  ottoInit();
}

void loop() {
  slideBack();
}
