#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "certificados.h"
#include <MQTT.h>

String MQTT_HOST = "ny3.blynk.cloud";
int MQTT_PORT = 8883;              // TLS
String MQTT_USER = "device";             // fixo
String BLYNK_AUTH_TOKEN = "xLNpnJ0e6f3JaeamhnZuKbRhxakC9lhK";   // password do MQTT

WiFiClientSecure conexaoSegura;
MQTTClient mqtt(1000);

int sensorLuz = 18; // Pino 10
unsigned long instanteAnterior = 0;


void reconectarWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin("Projeto", "2022-11-07");

        Serial.print("Conectando ao WiFi...");
        while (WiFi.status() != WL_CONNECTED) {
            Serial.print(".");
            delay(1000);
        }
    Serial.print("conectado!\nEndereço IP: ");
    Serial.println(WiFi.localIP());
    }
}

void reconectarMQTT() {
    if (!mqtt.connected()) {
        Serial.print("Conectando MQTT...");
        while(!mqtt.connected()) {
            mqtt.connect("GabrielMm47-Esp32", "device", "xLNpnJ0e6f3JaeamhnZuKbRhxakC9lhK");
            Serial.print(".");
            delay(1000);
        }
        Serial.println(" conectado!");

        mqtt.subscribe("downlink/#");
    }
}

void recebeuMensagem(String topico, String conteudo) {
    Serial.println(topico + ": " + conteudo);
    if (topico.endsWith(String("downlink/ds/LED"))) {
        int valor = conteudo.toInt();
        Serial.println("Valor recebido: " + String(valor));
        if (valor == 1) {
            rgbLedWrite(RGB_BUILTIN, 0, 50, 0);
        }
        else {
            rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
        }
    }
}

void setup() {
    Serial.begin(115200); delay(500);
    reconectarWiFi();
    conexaoSegura.setCACert(certificado1);

    mqtt.begin("ny3.blynk.cloud", 8883, conexaoSegura);
    mqtt.onMessage(recebeuMensagem);
    mqtt.setKeepAlive(45);
    //mqtt.setWill("tópico da desconexão", "conteúdo");

    reconectarMQTT();

}

void loop() {
    reconectarWiFi();
    reconectarMQTT();
    mqtt.loop();

    unsigned long instanteAtual = millis();
    if (instanteAtual > instanteAnterior + 5000) {  
        int leitura = analogRead(sensorLuz);
        int porcentagemLuz = map(leitura, 0, 4095, 0, 100);
        Serial.print("Luz: "); Serial.print(porcentagemLuz); Serial.println("%");
        String conteudo = String(porcentagemLuz);
        mqtt.publish("ds/LightSensor", conteudo);

        instanteAnterior = instanteAtual;
    }
}