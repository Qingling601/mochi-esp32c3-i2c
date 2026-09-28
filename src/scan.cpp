// I2C 扫描测试程序（独立构建环境 scan，不参与主固件）
// 目的：确认新买的 4 针 I2C OLED 的地址，以及 SCL/SDA 接线是否正确。
// 用法：
//   pio run -e scan -t upload    # 烧录扫描程序
//   pio device monitor           # 用波特率 115200 看扫描结果
#include <Arduino.h>
#include <Wire.h>

#define PIN_SCL 6
#define PIN_SDA 7

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("I2C scan starting (SCL=GPIO6, SDA=GPIO7)...");
  Wire.begin(PIN_SDA, PIN_SCL);
}

void loop() {
  int found = 0;
  Serial.println("Scanning 0x00 - 0x7F ...");
  for (uint8_t addr = 1; addr < 0x7F; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  FOUND device at 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  No I2C device found! Check SCL/SDA wiring & power.");
  } else {
    Serial.printf("Done. %d device(s) found.\n", found);
  }
  Serial.println("-----------------------------");
  delay(3000);
}
