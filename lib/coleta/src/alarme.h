#pragma once

#include <cstddef>
#include <cstdint>

namespace coleta {

// Falhas que o buzzer anuncia. A ordem é a do toque quando há mais de uma.
enum class Falha : uint8_t {
  Sensor,        // algum sensor sem leitura válida
  Conexao,       // Wi-Fi ou MQTT fora do ar
  Hora,          // relógio sem NTP: nada é publicado
  Configuracao,  // config.h inválido: nada é coletado
};

constexpr size_t QUANTIDADE_FALHAS = 4;

// Falha passageira não toca: só anuncia depois de durar esse tempo.
// Configuração inválida não se resolve sozinha, então toca na hora.
constexpr uint32_t PERSISTENCIA_MS = 30UL * 60UL * 1000UL;
// Enquanto a falha durar, o toque se repete nesse intervalo.
constexpr uint32_t REPETICAO_MS = 30UL * 60UL * 1000UL;

uint32_t persistenciaMs(Falha falha);
uint8_t bipesDe(Falha falha);  // código sonoro: 2, 3, 4 e 5 bipes
const char* nomeDe(Falha falha);

constexpr uint8_t mascaraDe(Falha falha) { return static_cast<uint8_t>(1u << static_cast<uint8_t>(falha)); }

// O que tocar agora. falhas junta mascaraDe() das falhas.
struct Aviso {
  uint8_t falhas = 0;
  bool normalizou = false;  // tudo voltou depois de um alarme
  bool vazio() const { return falhas == 0 && !normalizou; }
};

// Decide quando o buzzer toca, a partir do relógio monotônico (millis()).
// Toca quando uma falha passa da persistência, de novo se surgir outra falha
// confirmada, e a cada REPETICAO_MS enquanto durar. Quando todas somem depois
// de um alarme, avisa uma vez que normalizou.
class Alarme {
 public:
  void atualizar(Falha falha, bool ativa, uint32_t agoraMs);
  Aviso verificar(uint32_t agoraMs);
  uint8_t confirmadas(uint32_t agoraMs) const;

 private:
  bool ativa_[QUANTIDADE_FALHAS] = {};
  uint32_t desdeMs_[QUANTIDADE_FALHAS] = {};
  uint8_t anunciadas_ = 0;
  bool alarmado_ = false;
  uint32_t ultimoToqueMs_ = 0;
};

// Toca um Aviso sem bloquear o loop: diz se o buzzer deve estar ligado agora.
// Cada falha vira um grupo de bipes curtos; "normalizou" é um bipe longo.
class Bipador {
 public:
  static constexpr uint32_t BIPE_MS = 150;
  static constexpr uint32_t PAUSA_BIPE_MS = 150;
  static constexpr uint32_t PAUSA_GRUPO_MS = 1000;
  static constexpr uint32_t BIPE_NORMALIZOU_MS = 600;

  void tocar(const Aviso& aviso, uint32_t agoraMs);
  bool ligado(uint32_t agoraMs) const;
  bool tocando(uint32_t agoraMs) const;
  uint32_t duracaoMs() const;

 private:
  static constexpr size_t MAXIMO_TRECHOS = 40;
  uint32_t trechosMs_[MAXIMO_TRECHOS] = {};  // ligado, desligado, ligado...
  size_t quantidade_ = 0;
  uint32_t inicioMs_ = 0;

  void acrescentar(uint32_t ligadoMs, uint32_t desligadoMs);
};

}  // namespace coleta
