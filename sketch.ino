/*
 * Objetos Inteligentes Conectados - Projeto final (protótipo virtual)
 * Botao de emergencia conectado - ESP32-DevKitC V4 + DHT22 + LED + buzzer
 *
 * MQTT: broker.emqx.io:1883 (broker PUBLICO e sem autenticacao)
 * Wi-Fi: Wokwi-GUEST (rede virtual do simulador)
 * Somente para demonstracao academica; NAO e equipamento medico.
 * Telegram NAO implementado nesta primeira versao.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <math.h>
#include <string.h>

// PINOS: iguais aos descritos no artigo da Aula 3
constexpr uint8_t PIN_DHT = 15;
constexpr uint8_t PIN_BOTAO = 18;
constexpr uint8_t PIN_LED = 23;
constexpr uint8_t PIN_BUZZER = 25;

// REDE VIRTUAL E SERVIDOR MQTT
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.emqx.io";
constexpr uint16_t MQTT_PORT = 1883;

// Este prefixo separa nossos testes dos demais usuarios do broker publico.
#define TOPIC_PREFIX "mackenzie/oic/botao_b71f92"
const char* TOPIC_TEMP       = TOPIC_PREFIX "/temperatura";
const char* TOPIC_UMIDADE    = TOPIC_PREFIX "/umidade";
const char* TOPIC_EMERGENCIA = TOPIC_PREFIX "/emergencia";
const char* TOPIC_STATUS     = TOPIC_PREFIX "/status";
const char* TOPIC_COMANDO    = TOPIC_PREFIX "/comando";
// Topicos exclusivos para os testes de tempo (sem interferir nas funcoes normais)
const char* TOPIC_MEDIR_PEDIDO = TOPIC_PREFIX "/medicao/pedido";
const char* TOPIC_MEDIR_RESPOSTA = TOPIC_PREFIX "/medicao/resposta";

constexpr unsigned long INTERVALO_SENSOR_MS = 5000;
constexpr unsigned long DURACAO_ALARME_MS = 4000;
constexpr unsigned long DEBOUNCE_MS = 50;
constexpr unsigned long TENTATIVA_REDE_MS = 4000;  // Reconexao MQTT
constexpr unsigned long TENTATIVA_WIFI_MS = 60000; // intervalo de retentativas Wi-Fi

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
DHTesp dht;

bool alarmeAtivo = false;
unsigned long inicioAlarme = 0;
unsigned long ultimaLeitura = 0;
unsigned long ultimaTentativaWiFi = 0;
bool wifiTentativaIniciada = false;
unsigned long ultimaTentativaMqtt = 0;
// Medicoes temporarias: cada atuador e ligado por 500 ms, sem bloquear o loop.
unsigned long inicioLedTeste = 0;
unsigned long inicioBuzzerTeste = 0;
bool ledTesteAtivo = false;
bool buzzerTesteAtivo = false;

int leituraAnteriorBotao = HIGH;
int estadoEstavelBotao = HIGH;
unsigned long instanteMudancaBotao = 0;

bool emergenciaPendente = false;
bool wifiJaAnunciado = false;
char eventoPendente[160] = {0};

void publicarStatus(const char* texto) {
  Serial.print("[STATUS] ");
  Serial.println(texto);
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_STATUS, texto);
  }
}

void pararAlarme() {
  if (!alarmeAtivo) return;
  alarmeAtivo = false;
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);
  publicarStatus("ALARME_DESLIGADO");
}

void enviarEmergencia(const char* origem) {
  char evento[160];
  snprintf(evento, sizeof(evento),
           "{\"evento\":\"AJUDA_SOLICITADA\",\"origem\":\"%s\",\"uptime_ms\":%lu}",
           origem, (unsigned long)millis());
  Serial.print("[EMERGENCIA] ");
  Serial.println(evento);
  if (!mqtt.connected() || !mqtt.publish(TOPIC_EMERGENCIA, evento)) {
    // Um unico alerta pendente e preservado para a proxima reconexao.
    strncpy(eventoPendente, evento, sizeof(eventoPendente) - 1);
    eventoPendente[sizeof(eventoPendente) - 1] = '\0';
    emergenciaPendente = true;
    Serial.println("[MQTT] Sem conexao: alerta aguardando reconexao.");
  } else {
    Serial.println("[MQTT] Emergencia publicada no broker.");
  }
}

void iniciarAlarme(const char* origem) {
  alarmeAtivo = true;
  inicioAlarme = millis();
  digitalWrite(PIN_LED, HIGH);
  tone(PIN_BUZZER, 1800);
  Serial.print("[ATUADORES] LED e buzzer acionados por: ");
  Serial.println(origem);
  publicarStatus("ALARME_ATIVO");
  enviarEmergencia(origem);
}


// Resposta de medicao: enviada DEPOIS de ler o sensor ou acionar o componente.
// O computador calcula o tempo total entre o pedido e a recepcao desta resposta.
void processarMedicao(byte* dados, unsigned int tamanho) {
  char pedido[48];
  if (tamanho == 0 || tamanho >= sizeof(pedido)) return;
  memcpy(pedido, dados, tamanho);
  pedido[tamanho] = '\0';

  char tipo[12];
  unsigned long id = 0;
  if (sscanf(pedido, "%11[^|]|%lu", tipo, &id) != 2 || id == 0) return;
  if (!mqtt.connected()) return;

  if (alarmeAtivo) {
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"tipo\":\"%s\",\"id\":%lu,\"erro\":\"alarme ativo\"}", tipo, id);
    mqtt.publish(TOPIC_MEDIR_RESPOSTA, resp);
    return;
  }
  char resposta[192];
  const unsigned long comecoUs = micros();

  if (strcmp(tipo, "SENSOR") == 0) {
    TempAndHumidity leitura = dht.getTempAndHumidity();
    const unsigned long fimUs = micros();
    if (isnan(leitura.temperature) || isnan(leitura.humidity)) {
      snprintf(resposta, sizeof(resposta), "{\"tipo\":\"SENSOR\",\"id\":%lu,\"erro\":\"leitura falhou\"}", id);
    } else {
      snprintf(resposta, sizeof(resposta),
          "{\"tipo\":\"SENSOR\",\"id\":%lu,\"tempo_local_ms\":%.3f,\"temperatura\":%.1f,\"umidade\":%.1f}",
          id, (fimUs-comecoUs)/1000.0, leitura.temperature, leitura.humidity);
    }
  } else if (strcmp(tipo, "LED") == 0) {
    digitalWrite(PIN_LED, HIGH);
    const unsigned long fimUs = micros();
    ledTesteAtivo = true;
    inicioLedTeste = millis();
    snprintf(resposta, sizeof(resposta),
      "{\"tipo\":\"LED\",\"id\":%lu,\"tempo_local_ms\":%.3f}",
      id, (fimUs-comecoUs)/1000.0);
  } else if (strcmp(tipo, "BUZZER") == 0) {
    tone(PIN_BUZZER, 1800);
    const unsigned long fimUs = micros();
    buzzerTesteAtivo = true;
    inicioBuzzerTeste = millis();
    snprintf(resposta, sizeof(resposta),
      "{\"tipo\":\"BUZZER\",\"id\":%lu,\"tempo_local_ms\":%.3f}",
      id, (fimUs-comecoUs)/1000.0);
  } else return;

  mqtt.publish(TOPIC_MEDIR_RESPOSTA, resposta, false);
  Serial.print("[MEDICAO] ");
  Serial.println(resposta);
}

void receberMqtt(char* topico, byte* dados, unsigned int tamanho) {
  if (strcmp(topico, TOPIC_MEDIR_PEDIDO) == 0) {
    processarMedicao(dados, tamanho);
    return;
  }
  if (strcmp(topico, TOPIC_COMANDO) != 0) return;

  char comando[32];
  if (tamanho == 0 || tamanho >= sizeof(comando)) return;
  memcpy(comando, dados, tamanho);
  comando[tamanho] = '\0';
  // Comandos intencionalmente simples para demonstracao em MQTTX.
  Serial.print("[MQTT] Comando recebido: ");
  Serial.println(comando);

  if (strcmp(comando, "ALERTA") == 0) {
    iniciarAlarme("MQTT");
  } else if (strcmp(comando, "PARAR") == 0) {
    pararAlarme();
  } else {
    publicarStatus("COMANDO_INVALIDO");
  }
}

void garantirWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiJaAnunciado) {
      Serial.println("[Wi-Fi] CONECTADO!");
      wifiJaAnunciado = true;
    }
    return;
  }

  wifiJaAnunciado = false;
  unsigned long agora = millis();
  // IMPORTANTE: nao reiniciar WiFi.begin() a cada 4 segundos.
  // No simulador, isso pode interromper a tentativa que ainda esta em andamento.
  if (wifiTentativaIniciada &&
      agora - ultimaTentativaWiFi < TENTATIVA_WIFI_MS) return;

  if (wifiTentativaIniciada) {
    Serial.print("[Wi-Fi] Ainda sem conexao apos 60 s. Status: ");
    Serial.println((int)WiFi.status());
    Serial.println("[Wi-Fi] Nova tentativa...");
  } else {
    Serial.println("[Wi-Fi] Iniciando conexao a Wokwi-GUEST...");
  }
  wifiTentativaIniciada = true;
  ultimaTentativaWiFi = agora;
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
}

void garantirMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  unsigned long agora = millis();
  if (ultimaTentativaMqtt != 0 && agora - ultimaTentativaMqtt < TENTATIVA_REDE_MS) return;
  ultimaTentativaMqtt = agora;

  // ID unico por conexao, evitando derrubar outra sessao no broker publico.
  char clientId[30];
  snprintf(clientId, sizeof(clientId), "oic-%08lx", (unsigned long)esp_random());
  Serial.print("[MQTT] Conectando a ");
  Serial.println(MQTT_HOST);
  if (mqtt.connect(clientId)) {
    Serial.println("[MQTT] CONECTADO!");
    if (mqtt.subscribe(TOPIC_COMANDO)) {
      Serial.print("[MQTT] Inscrito em: ");
      Serial.println(TOPIC_COMANDO);
    }
    if (mqtt.subscribe(TOPIC_MEDIR_PEDIDO)) {
      Serial.print("[MQTT] Inscrito para medicoes em: ");
      Serial.println(TOPIC_MEDIR_PEDIDO);
    }
    publicarStatus("ESP32_ONLINE");
    if (emergenciaPendente) {
      if (mqtt.publish(TOPIC_EMERGENCIA, eventoPendente)) {
        emergenciaPendente = false;
        Serial.println("[MQTT] Alerta pendente publicado.");
      }
    }
  } else {
    Serial.print("[MQTT] Falha (codigo ");
    Serial.print(mqtt.state());
    Serial.println("). Nova tentativa em alguns segundos.");
  }
}

void verificarBotao() {
  const unsigned long agora = millis();
  const int leitura = digitalRead(PIN_BOTAO);
  if (leitura != leituraAnteriorBotao) instanteMudancaBotao = agora;
  if (agora - instanteMudancaBotao >= DEBOUNCE_MS && leitura != estadoEstavelBotao) {
    estadoEstavelBotao = leitura;
    if (estadoEstavelBotao == LOW) {
      iniciarAlarme("BOTAO");
    }
  }
  leituraAnteriorBotao = leitura;
}

void publicarSensor() {
  if (millis() - ultimaLeitura < INTERVALO_SENSOR_MS) return;
  ultimaLeitura = millis();
  TempAndHumidity medida = dht.getTempAndHumidity();
  if (isnan(medida.temperature) || isnan(medida.humidity)) {
    Serial.println("[DHT22] Erro de leitura do sensor.");
    return;
  }

  Serial.print("[DHT22] Temperatura = ");
  Serial.print(medida.temperature, 1);
  Serial.print(" C | Umidade = ");
  Serial.print(medida.humidity, 1);
  Serial.println(" %");

  if (!mqtt.connected()) {
    Serial.println("[MQTT] Sem conexao: proxima leitura tentara novamente.");
    return;
  }
  char valor[20];
  snprintf(valor, sizeof(valor), "%.1f", medida.temperature);
  bool tempOk = mqtt.publish(TOPIC_TEMP, valor);
  snprintf(valor, sizeof(valor), "%.1f", medida.humidity);
  bool umidOk = mqtt.publish(TOPIC_UMIDADE, valor);
  Serial.println(tempOk && umidOk ? "[MQTT] Sensor publicado." : "[MQTT] Falha ao publicar sensor.");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOTAO, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  dht.setup(PIN_DHT, DHTesp::DHT22);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(receberMqtt);
  WiFi.mode(WIFI_STA);

  Serial.println("=== BOTAO DE EMERGENCIA - SIMULACAO ACADEMICA ===");
  Serial.println("Tópicos MQTT: " TOPIC_PREFIX "/#");

  // O MESMO procedimento do teste isolado que conseguiu IP 10.10.0.2.
  // Aguarda a conexao inicial sem reiniciar WiFi.begin() durante a tentativa.
  delay(1000);
  Serial.println("[Wi-Fi] Testando conexao ao Wokwi-GUEST...");
  WiFi.begin("Wokwi-GUEST", "", 6);
  wifiTentativaIniciada = true;
  ultimaTentativaWiFi = millis();

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 40) {
    delay(500);
    Serial.print(".");
    ++tentativas;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    wifiJaAnunciado = true;
    Serial.println("[Wi-Fi] CONECTADO!");
  } else {
    Serial.print("[Wi-Fi] FALHOU. Status: ");
    Serial.println((int)WiFi.status());
    Serial.println("[Wi-Fi] Tentaremos novamente sem reiniciar em loop a cada 4 s.");
  }
}

void loop() {
  // Desliga pulsos curtos de teste automaticamente.
  if (ledTesteAtivo && millis() - inicioLedTeste >= 500) {
    ledTesteAtivo = false;
    if (!alarmeAtivo) digitalWrite(PIN_LED, LOW);
  }
  if (buzzerTesteAtivo && millis() - inicioBuzzerTeste >= 500) {
    buzzerTesteAtivo = false;
    if (!alarmeAtivo) { noTone(PIN_BUZZER); digitalWrite(PIN_BUZZER, LOW); }
  }
  verificarBotao();  // Independente de ter Internet: alerta local funciona.
  if (alarmeAtivo && millis() - inicioAlarme >= DURACAO_ALARME_MS) {
    pararAlarme();
  }
  garantirWiFi();
  garantirMqtt();
  mqtt.loop();
  publicarSensor();
  delay(10);
}
