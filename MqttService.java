package com.ivguard.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.ivguard.model.SensorPayload;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import org.eclipse.paho.client.mqttv3.*;
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

/**
 * MQTT service.
 *
 * Subscribes to:   ivguard/+/sensor   (wildcard — all rooms)
 * Publishes to:    ivguard/{roomId}/valve   with payload "LOCK" | "UNLOCK"
 *
 * The ESP32-S3 Arduino sketch must:
 *   1. Publish JSON sensor data to  ivguard/{roomId}/sensor
 *   2. Subscribe to                 ivguard/{roomId}/valve
 *   3. On receiving "LOCK", actuate the servo to close position.
 */
@Service
public class MqttService implements MqttCallback {

    private static final Logger log = LoggerFactory.getLogger(MqttService.class);

    @Value("${ivguard.mqtt.broker-url}")
    private String brokerUrl;

    @Value("${ivguard.mqtt.client-id}")
    private String clientId;

    @Value("${ivguard.mqtt.username:}")
    private String username;

    @Value("${ivguard.mqtt.password:}")
    private String password;

    private MqttClient client;
    private final ObjectMapper mapper = new ObjectMapper();

    @Autowired
    private SensorProcessingService sensorService;

    @PostConstruct
    public void connect() {
        try {
            client = new MqttClient(brokerUrl, clientId, new MemoryPersistence());
            MqttConnectOptions opts = new MqttConnectOptions();
            opts.setCleanSession(true);
            opts.setAutomaticReconnect(true);
            if (!username.isEmpty()) {
                opts.setUserName(username);
                opts.setPassword(password.toCharArray());
            }
            client.setCallback(this);
            client.connect(opts);

            /* Subscribe to sensor data from all rooms */
            client.subscribe("ivguard/+/sensor", 1);
            log.info("MQTT connected to {} — subscribed to ivguard/+/sensor", brokerUrl);
        } catch (MqttException e) {
            log.warn("MQTT unavailable ({}). Valve commands will be no-ops until broker is reachable.", e.getMessage());
        }
    }

    /**
     * Send LOCK or UNLOCK to the ESP32-S3 for a specific room.
     * QoS 2 = exactly-once delivery.
     */
    public void sendValveCommand(String roomId, String command) {
        if (client == null || !client.isConnected()) {
            log.warn("MQTT not connected — cannot send valve command '{}' to room {}", command, roomId);
            return;
        }
        String topic = "ivguard/" + roomId + "/valve";
        try {
            MqttMessage msg = new MqttMessage(command.getBytes());
            msg.setQos(2);
            msg.setRetained(true);   // ESP32 gets it even if it reconnects
            client.publish(topic, msg);
            log.info("MQTT → {} : {}", topic, command);
        } catch (MqttException e) {
            log.error("MQTT publish error: {}", e.getMessage());
        }
    }

    /* ── MqttCallback ── */

    @Override
    public void messageArrived(String topic, MqttMessage message) {
        try {
            String json = new String(message.getPayload());
            log.debug("MQTT ← {} : {}", topic, json);
            SensorPayload payload = mapper.readValue(json, SensorPayload.class);
            /* Extract roomId from topic: ivguard/{roomId}/sensor */
            String[] parts = topic.split("/");
            if (parts.length >= 2) payload.setRoomId(parts[1]);
            sensorService.process(payload);
        } catch (Exception e) {
            log.error("Failed to parse MQTT sensor message from {}: {}", topic, e.getMessage());
        }
    }

    @Override
    public void connectionLost(Throwable cause) {
        log.warn("MQTT connection lost: {}", cause.getMessage());
    }

    @Override
    public void deliveryComplete(IMqttDeliveryToken token) {
        // no-op
    }

    @PreDestroy
    public void disconnect() {
        try {
            if (client != null && client.isConnected()) client.disconnect();
        } catch (MqttException ignored) {}
    }
}
