#pragma once

#include "pinos.h"
#include "sensores.h"

// Canais da placa (SCRUM-621). Todos são lidos e enviados em toda coleta, com
// o valor bruto; não é preciso mexer aqui para ligar um sensor novo:
//   1. ligue o sensor em um canal livre do tipo certo (docs/hardware.md);
//   2. no sistema, cadastre o sensor na estação com o identificador do canal
//      e o fator/ganho/offset que convertem o bruto para a unidade do tipo.
// Canal sem sensor cadastrado é ignorado pelo tratamento.
constexpr coleta::Sensor SENSORES[] = {
    {"temp", coleta::Fonte::Dht22Temperatura, PINO_DHT},  // °C
    {"umid", coleta::Fonte::Dht22Umidade, PINO_DHT},      // %
    {"an32", coleta::Fonte::Analogico, 32},               // 0 a 4095 (0 a 3,3 V)
    {"an33", coleta::Fonte::Analogico, 33},
    {"an34", coleta::Fonte::Analogico, 34},
    {"an35", coleta::Fonte::Analogico, 35},
    {"an36", coleta::Fonte::Analogico, 36},
    {"an39", coleta::Fonte::Analogico, 39},
    {"pu26", coleta::Fonte::Pulsos, 26},                  // pulsos desde o último envio
    {"pu27", coleta::Fonte::Pulsos, 27},
    {"di18", coleta::Fonte::Digital, 18},                 // 0 = contato fechado ao GND
    {"di19", coleta::Fonte::Digital, 19},
};

constexpr size_t QUANTIDADE_SENSORES = sizeof(SENSORES) / sizeof(SENSORES[0]);
static_assert(QUANTIDADE_SENSORES <= coleta::MAXIMO_SENSORES, "canais demais em SENSORES");

constexpr bool pinoLivre(int pino, size_t i = 0) {
  return i == QUANTIDADE_SENSORES || (SENSORES[i].pino != pino && pinoLivre(pino, i + 1));
}
static_assert(pinoLivre(PINO_BUZZER), "o buzzer usa o pino de um canal");
