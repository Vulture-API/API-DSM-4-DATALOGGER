#include "alarme.h"

namespace coleta {

namespace {
size_t indice(Falha falha) { return static_cast<size_t>(falha); }
}  // namespace

uint32_t persistenciaMs(Falha falha) { return falha == Falha::Configuracao ? 0 : PERSISTENCIA_MS; }

uint8_t bipesDe(Falha falha) { return static_cast<uint8_t>(indice(falha) + 2); }

const char* nomeDe(Falha falha) {
  switch (falha) {
    case Falha::Sensor:
      return "sensor";
    case Falha::Conexao:
      return "conexao";
    case Falha::Hora:
      return "hora";
    case Falha::Configuracao:
      return "configuracao";
  }
  return "?";
}

void Alarme::atualizar(Falha falha, bool ativa, uint32_t agoraMs) {
  const size_t i = indice(falha);
  if (ativa && !ativa_[i]) desdeMs_[i] = agoraMs;
  ativa_[i] = ativa;
}

uint8_t Alarme::confirmadas(uint32_t agoraMs) const {
  uint8_t mascara = 0;
  for (size_t i = 0; i < QUANTIDADE_FALHAS; i++) {
    const Falha falha = static_cast<Falha>(i);
    if (ativa_[i] && agoraMs - desdeMs_[i] >= persistenciaMs(falha)) mascara |= mascaraDe(falha);
  }
  return mascara;
}

Aviso Alarme::verificar(uint32_t agoraMs) {
  Aviso aviso;
  const uint8_t atuais = confirmadas(agoraMs);
  if (atuais == 0) {
    aviso.normalizou = alarmado_;
    alarmado_ = false;
    anunciadas_ = 0;
    return aviso;
  }
  const bool novaFalha = (atuais & ~anunciadas_) != 0;
  const bool horaDeRepetir = alarmado_ && agoraMs - ultimoToqueMs_ >= REPETICAO_MS;
  anunciadas_ = atuais;  // falha que sumiu sai da lista sem tocar
  if (novaFalha || horaDeRepetir) {
    aviso.falhas = atuais;
    alarmado_ = true;
    ultimoToqueMs_ = agoraMs;
  }
  return aviso;
}

void Bipador::acrescentar(uint32_t ligadoMs, uint32_t desligadoMs) {
  if (quantidade_ + 2 > MAXIMO_TRECHOS) return;
  trechosMs_[quantidade_++] = ligadoMs;
  trechosMs_[quantidade_++] = desligadoMs;
}

void Bipador::tocar(const Aviso& aviso, uint32_t agoraMs) {
  quantidade_ = 0;
  inicioMs_ = agoraMs;
  for (size_t i = 0; i < QUANTIDADE_FALHAS; i++) {
    const Falha falha = static_cast<Falha>(i);
    if ((aviso.falhas & mascaraDe(falha)) == 0) continue;
    for (uint8_t b = 0; b < bipesDe(falha); b++) {
      const bool ultimo = b + 1 == bipesDe(falha);
      acrescentar(BIPE_MS, ultimo ? PAUSA_GRUPO_MS : PAUSA_BIPE_MS);
    }
  }
  if (aviso.falhas == 0 && aviso.normalizou) acrescentar(BIPE_NORMALIZOU_MS, 0);
}

uint32_t Bipador::duracaoMs() const {
  uint32_t total = 0;
  for (size_t i = 0; i < quantidade_; i++) total += trechosMs_[i];
  return total;
}

bool Bipador::tocando(uint32_t agoraMs) const { return agoraMs - inicioMs_ < duracaoMs(); }

bool Bipador::ligado(uint32_t agoraMs) const {
  uint32_t decorrido = agoraMs - inicioMs_;
  for (size_t i = 0; i < quantidade_; i++) {
    if (decorrido < trechosMs_[i]) return i % 2 == 0;
    decorrido -= trechosMs_[i];
  }
  return false;
}

}  // namespace coleta
