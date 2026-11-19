#pragma once

#include <cstdint>

#if defined(__GNUC__)
#define COLETA_NO_ISR inline __attribute__((always_inline))
#else
#define COLETA_NO_ISR inline
#endif

namespace coleta {

// Contador de pulsos de sensor basculante (pluviômetro com reed switch).
// registrar() roda dentro da interrupção: sem chamadas, sem alocação.
// Os pulsos só saem da contagem depois que a leitura foi publicada
// (descontar), então uma queda do MQTT não perde chuva: o que ficou
// pendente vai junto no próximo envio.
struct ContadorPulsos {
  // O reed quica ao fechar; pulsos mais próximos que isso contam como um só.
  static constexpr uint32_t DEBOUNCE_MS = 50;

  volatile uint32_t pendentes = 0;
  volatile uint32_t ultimoMs = 0;
  volatile bool tem_ultimo = false;

  COLETA_NO_ISR bool registrar(uint32_t agoraMs) {
    if (tem_ultimo && static_cast<uint32_t>(agoraMs - ultimoMs) < DEBOUNCE_MS) return false;
    tem_ultimo = true;
    ultimoMs = agoraMs;
    pendentes = pendentes + 1;
    return true;
  }

  uint32_t total() const { return pendentes; }

  // Tira da contagem só o que foi publicado; pulsos que chegaram depois ficam.
  void descontar(uint32_t publicados) { pendentes = publicados >= pendentes ? 0 : pendentes - publicados; }
};

}  // namespace coleta
