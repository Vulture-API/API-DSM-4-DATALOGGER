#pragma once

// DHT22/AM2302: dados no GPIO4 com resistor de pull-up de 10 kΩ para 3V3
// (o módulo com placa já traz o resistor).
constexpr int PINO_DHT = 4;

// Buzzer ativo (só liga/desliga) no GPIO26, que é o fio Modulo de um dos
// conectores da placa Fatec/Tecsus e não é pino de boot. O buzzer
// solto puxa ~25 mA: ligue por um transistor (BC547 com 1 kΩ na base). Módulo
// de 3 pinos que apita com o I/O em nível baixo: troque para false.
constexpr int PINO_BUZZER = 26;
constexpr bool BUZZER_LIGA_EM_ALTO = false;  // módulo de 3 pinos da bancada liga em nível baixo
// Diferente de 0: o buzzer toca as notas das melodias (PWM 50%), o que faz
// soar buzzer passivo e ativo. 0 = só liga/desliga (buzzer ativo puro, sem
// melodia). O valor também configura o canal PWM no início.
constexpr unsigned BUZZER_FREQUENCIA_HZ = 2700;
