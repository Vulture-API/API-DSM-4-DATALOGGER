#pragma once

#include <cstddef>

#include "pinos.h"

// Pinos da placa que o datalogger lê em toda coleta (SCRUM-621). Não é preciso
// mexer aqui para ligar um sensor: ligue em qualquer conector, veja no payload
// (ou no comando "canais" da serial) qual canal mudou e cadastre o sensor na
// estação com esse identificador e o fator/ganho/offset da conversão.
//   anNN  valor do ADC, 0 a 4095 (0 a 3,3 V)    pinos analógicos
//   puNN  pulsos desde o último envio            todos os pinos
//   temp/umid  DHT22, procurado em PINOS_DIGITAIS (começa por PINO_DHT)
// Canal sem sensor cadastrado é ignorado pelo tratamento.
//
// Analógicos: só o ADC1 lê com o Wi-Fi ligado. Ficam os GPIO 34 a 39, que
// só são entrada e não têm pull-up (contato seco ali precisa do resistor da
// placa). 32 e 33 são lidos como digitais: com cabo longo e sem pull-up, a
// leitura analógica pegava ruído da rede e contava pulso falso.
constexpr int PINOS_ANALOGICOS[] = {34, 35, 36, 39};
// Digitais com pull-up interno. 5, 12 e 15 só são lidos depois do boot (o
// pull-up volta ao padrão no reset). Ficam fora 0 e 2 (boot e LED), a serial
// (1, 3), a flash (6 a 11) e o buzzer.
constexpr int PINOS_DIGITAIS[] = {PINO_DHT, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 27, 32, 33};

constexpr size_t QUANTIDADE_ANALOGICOS = sizeof(PINOS_ANALOGICOS) / sizeof(PINOS_ANALOGICOS[0]);
constexpr size_t QUANTIDADE_DIGITAIS = sizeof(PINOS_DIGITAIS) / sizeof(PINOS_DIGITAIS[0]);

constexpr bool semBuzzer(const int* pinos, size_t n, size_t i = 0) {
  return i == n || (pinos[i] != PINO_BUZZER && semBuzzer(pinos, n, i + 1));
}
static_assert(semBuzzer(PINOS_ANALOGICOS, QUANTIDADE_ANALOGICOS), "o buzzer usa um pino analógico");
static_assert(semBuzzer(PINOS_DIGITAIS, QUANTIDADE_DIGITAIS), "o buzzer usa um pino digital");
