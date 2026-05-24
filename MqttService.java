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

            client.subscribe("ivguard/+/sensor", 1);
            log.info("MQTT connected to {} — subscribed to ivguard/+/sensor", brokerUrl);
        } catch (MqttException e) {
            log.warn("MQTT unavailable ({}). Valve commands will be no-ops until broker is reachable.", e.getMessage());
        }
    }

    public void
