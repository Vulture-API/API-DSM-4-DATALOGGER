#include "intervalo.h"

namespace coleta {

bool intervaloValido(uint32_t segundos) {
  return segundos >= INTERVALO_MINIMO_S && segundos <= INTERVALO_MAXIMO_S;
}

Agendador::Agendador(uint32_t intervaloS)
    : intervaloS_(intervaloValido(intervaloS) ? intervaloS : INTERVALO_PADRAO_S) {}

bool Agendador::configurar(uint32_t segundos) {
  if (!intervaloValido(segundos)) {
    return false;
  }
  intervaloS_ = segundos;
  return true;
}

bool Agendador::deveColetar(uint32_t agoraMs) {
  if (primeira_) {
    primeira_ = false;
    ultimaMs_ = agoraMs;
    return true;
  }
  // Subtração sem sinal: continua certa quando millis() dá a volta (~49 dias).
  if (agoraMs - ultimaMs_ < intervaloS_ * 1000UL) {
    return false;
  }
  ultimaMs_ = agoraMs;
  return true;
}

}  // namespace coleta
