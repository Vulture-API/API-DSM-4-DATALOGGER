#include "canais.h"

#include <cstdio>

namespace coleta {

size_t montarCanais(int pinoDht, const int* analogicos, size_t qtdAnalogicos, const int* digitais,
                    size_t qtdDigitais, Sensor* destino, char (*nomes)[TAMANHO_NOME_CANAL], size_t maximo) {
  size_t n = 0;
  auto incluir = [&](const char* prefixo, Fonte fonte, int pino) {
    if (n >= maximo) return;
    std::snprintf(nomes[n], TAMANHO_NOME_CANAL, "%s%d", prefixo, pino);
    destino[n] = Sensor{nomes[n], fonte, pino};
    n++;
  };
  if (pinoDht >= 0 && n + 2 <= maximo) {
    destino[n++] = Sensor{"temp", Fonte::Dht22Temperatura, pinoDht};
    destino[n++] = Sensor{"umid", Fonte::Dht22Umidade, pinoDht};
  }
  for (size_t i = 0; i < qtdAnalogicos; i++) incluir("an", Fonte::Analogico, analogicos[i]);
  for (size_t i = 0; i < qtdAnalogicos; i++) incluir("pu", Fonte::Pulsos, analogicos[i]);
  for (size_t i = 0; i < qtdDigitais; i++) {
    if (digitais[i] != pinoDht) incluir("pu", Fonte::Pulsos, digitais[i]);
  }
  return n;
}

}  // namespace coleta
