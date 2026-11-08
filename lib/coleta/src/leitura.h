#pragma once

namespace coleta {

// Faixa física do DHT22/AM2302. Fora dela a leitura é tratada como falha do
// sensor; limites de negócio ficam no tratamento (SCRUM-448).
constexpr float TEMPERATURA_MIN_C = -40.0f;
constexpr float TEMPERATURA_MAX_C = 80.0f;
constexpr float UMIDADE_MIN = 0.0f;
constexpr float UMIDADE_MAX = 100.0f;

struct Leitura {
  float temperatura = 0;
  float umidade = 0;
  bool temperaturaOk = false;
  bool umidadeOk = false;

  bool completa() const { return temperaturaOk && umidadeOk; }
  bool vazia() const { return !temperaturaOk && !umidadeOk; }
};

Leitura avaliarLeitura(float temperatura, float umidade);

}  // namespace coleta
