#pragma once

#include "pinos.h"
#include "sensores.h"

// Sensores desta estação. Cada linha vira um campo do payload MQTT:
//   identificador  local_identifier do sensor cadastrado para a estação no
//                  sistema (/api/sensors); o tratamento só associa a leitura
//                  se o identificador bater
//   fonte          de onde vem a leitura (ver coleta::Fonte)
//   pino           GPIO do sensor
//
// Para incluir um sensor, acrescente uma linha aqui e cadastre o sensor na
// estação com o mesmo identificador. O valor enviado é o bruto: a conversão
// para a unidade do parâmetro usa o ganho e o offset do cadastro.
// Entrada analógica só nos GPIO 32 a 39 (os outros não leem com o Wi-Fi ligado).
constexpr coleta::Sensor SENSORES[] = {
    {"temp", coleta::Fonte::Dht22Temperatura, PINO_DHT},
    {"umid", coleta::Fonte::Dht22Umidade, PINO_DHT},
    // {"lux", coleta::Fonte::Analogico, 34},    // LDR em divisor de tensão
    // {"porta", coleta::Fonte::Digital, 27},    // contato seco
};

constexpr size_t QUANTIDADE_SENSORES = sizeof(SENSORES) / sizeof(SENSORES[0]);
static_assert(QUANTIDADE_SENSORES <= coleta::MAXIMO_SENSORES, "sensores demais em SENSORES");
