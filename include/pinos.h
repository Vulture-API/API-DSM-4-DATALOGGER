#pragma once

// DHT22/AM2302: dados no GPIO4 com resistor de pull-up de 10 kΩ para 3V3
// (o módulo com placa já traz o resistor).
constexpr int PINO_DHT = 4;

// Buzzer ativo (só liga/desliga) no GPIO25, que não é pino de boot. O buzzer
// solto puxa ~25 mA: ligue por um transistor (BC547 com 1 kΩ na base). Módulo
// de 3 pinos que apita com o I/O em nível baixo: troque para false.
constexpr int PINO_BUZZER = 25;
constexpr bool BUZZER_LIGA_EM_ALTO = true;
