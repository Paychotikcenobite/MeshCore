#include <Arduino.h>
#include <Wire.h>
#include "TDeckBoard.h"

uint32_t deviceOnline = 0x00;

void TDeckBoard::begin() {
  
  ESP32Board::begin();
  
  // Enable peripheral power
  pinMode(PIN_PERF_POWERON, OUTPUT);
  digitalWrite(PIN_PERF_POWERON, HIGH);

#ifdef MESHCORE_COMPACT_UI
  // The standard T-Deck touch controller and keyboard share the board I2C bus.
  // Match LILYGO/Launcher hardware initialization: SDA 18, SCL 8.
  // The short delay gives the on-board ESP32-C3 keyboard/peripherals time to boot.
  delay(500);
  Wire.begin(18, 8);
#endif

  // Configure user button
  pinMode(PIN_USER_BTN, INPUT);

  // Configure LoRa Pins
  pinMode(P_LORA_MISO, INPUT_PULLUP);
  // pinMode(P_LORA_DIO_1, INPUT_PULLUP);

  #ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, HIGH); // inverted pin for SX1276 - HIGH for off
  #endif

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    long wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1 << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET; // received a LoRa packet (while in deep sleep)
    }

    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }
}