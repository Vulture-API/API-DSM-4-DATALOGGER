#pragma once

#include <cstdint>

namespace coleta {

// Contagem de pulsos por amostragem do nível do pino (SCRUM-621). Uma tarefa
// lê todos os canais a cada PERIODO_AMOSTRAGEM_MS, então qualquer pino serve,
// inclusive os GPIO 34 a 39, que não têm pull-up interno nem interrupção
// confiável sem resistor na placa.
constexpr uint32_t PERIODO_AMOSTRAGEM_MS = 5;

// Entrada analógica lida como pulso: histerese entre 1/3 e 2/3 da escala.
constexpr int LIMIAR_BAIXO = 1365;
constexpr int LIMIAR_ALTO = 2730;

inline bool nivelAnalogico(int valor, bool anterior) {
  if (valor <= LIMIAR_BAIXO) return false;
  if (valor >= LIMIAR_ALTO) return true;
  return anterior;
}

struct CanalPulsos {
  // O nível só muda depois de repetir em amostras seguidas: o quique do reed
  // (poucos ms) não vira pulso.
  static constexpr uint8_t AMOSTRAS_ESTAVEIS = 2;

  bool iniciado = false;
  bool alto = true;
  uint8_t contrarias = 0;
  uint32_t pendentes = 0;

  // Conta a borda de descida confirmada (contato fechando).
  bool amostrar(bool nivelAlto) {
    if (!iniciado) {
      iniciado = true;
      alto = nivelAlto;
      return false;
    }
    if (nivelAlto == alto) {
      contrarias = 0;
      return false;
    }
    if (++contrarias < AMOSTRAS_ESTAVEIS) return false;
    alto = nivelAlto;
    contrarias = 0;
    if (alto) return false;
    pendentes++;
    return true;
  }

  uint32_t total() const { return pendentes; }

  // Tira da contagem só o que foi publicado; pulsos que chegaram depois ficam.
  void descontar(uint32_t publicados) { pendentes = publicados >= pendentes ? 0 : pendentes - publicados; }
};

}  // namespace coleta
