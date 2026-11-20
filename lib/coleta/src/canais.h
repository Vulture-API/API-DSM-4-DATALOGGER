#pragma once

#include <cstddef>

#include "sensores.h"

namespace coleta {

constexpr size_t TAMANHO_NOME_CANAL = 8;

// Monta os canais da estação (SCRUM-621): "temp" e "umid" se houver DHT22,
// "anNN" (valor do ADC) e "puNN" (pulsos) para cada pino analógico, e "puNN"
// para cada pino digital, menos o do DHT22. pinoDht < 0: sem DHT22.
// Devolve quantos canais cabem em destino.
bool contemPino(const int* pinos, size_t quantidade, int pino);

size_t montarCanais(int pinoDht, const int* analogicos, size_t qtdAnalogicos, const int* digitais,
                    size_t qtdDigitais, Sensor* destino, char (*nomes)[TAMANHO_NOME_CANAL], size_t maximo);

}  // namespace coleta
