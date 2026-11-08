#include "leitura.h"

#include <cmath>

namespace coleta {

namespace {
bool dentro(float valor, float minimo, float maximo) {
  return std::isfinite(valor) && valor >= minimo && valor <= maximo;
}
}  // namespace

Leitura avaliarLeitura(float temperatura, float umidade) {
  Leitura leitura;
  leitura.temperaturaOk = dentro(temperatura, TEMPERATURA_MIN_C, TEMPERATURA_MAX_C);
  leitura.umidadeOk = dentro(umidade, UMIDADE_MIN, UMIDADE_MAX);
  if (leitura.temperaturaOk) leitura.temperatura = temperatura;
  if (leitura.umidadeOk) leitura.umidade = umidade;
  return leitura;
}

}  // namespace coleta
