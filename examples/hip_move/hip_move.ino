#include <OttoI2R.h>

// [사용 방법] 시리얼 모니터(115200)에 두 숫자를 띄어서 입력하고 Enter를 누르세요.
//   80 100  -> moveHip(80, 100): 오른쪽 엉덩이(YR)는 80도, 왼쪽 엉덩이(YL)는 100도로 움직입니다.
//   각도는 0 ~ 180 사이이고, 90이 가운데입니다. 발(RL, RR)은 움직이지 않습니다.

// 보정값은 웹의 [로봇 맞추기]에서 맞춰 보드에 저장한 값을 쓴다.
// 아직 저장하지 않은 보드는 기본값으로 움직인다. 로봇에 따라 안 맞을 수 있으니 먼저 [로봇 맞추기]를 하세요.

void setup() {
  ottoInit();
  Serial.begin(115200);
  Serial.println("입력 예: 80 100  (YR 80도, YL 100도)");
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    int space = line.indexOf(' ');

    if (space > 0) {
      int yr = line.substring(0, space).toInt();   // 첫 번째 숫자: 오른쪽 엉덩이
      int yl = line.substring(space + 1).toInt();  // 두 번째 숫자: 왼쪽 엉덩이
      moveHip(yr, yl);
      Serial.print("오른쪽 엉덩이(YR) = ");
      Serial.print(yr);
      Serial.print(", 왼쪽 엉덩이(YL) = ");
      Serial.println(yl);
    } else {
      Serial.println("두 숫자를 띄어서 입력하세요. 예: 80 100");
    }
  }
}
