package com.ivguard.controller;

import com.ivguard.model.ClampRequest;
import com.ivguard.model.SensorPayload;
import com.ivguard.service.MqttService;
import com.ivguard.service.SensorProcessingService;
import com.ivguard.websocket.WebSocketBroadcaster;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.time.Instant;
import java.util.Map;

@RestController
@RequestMapping("/api")
@CrossOrigin(origins = "*")
public class IVGuardController {

    private static final Logger log = LoggerFactory.getLogger(IVGuardController.class);

    private final SensorProcessingService sensorService;
    private final MqttService mqttService;
    private final WebSocketBroadcaster broadcaster;

    public IVGuardController(SensorProcessingService sensorService,
                             MqttService mqttService,
                             WebSocketBroadcaster broadcaster) {
        this.sensorService = sensorService;
        this.mqttService = mqttService;
        this.broadcaster = broadcaster;
    }

    @PostMapping("/sensor-data")
    public ResponseEntity<Map<String, Object>> receiveSensorData(
            @RequestBody SensorPayload payload) {

        log.debug("POST /api/sensor-data ← Room {} | vol={}/{} dpm={}",
            payload.getRoomId(), payload.getCurrentVolume(),
            payload.getTotalVolume(), payload.getDpm());

        if (payload.getRoomId() == null || payload.getTotalVolume() <= 0) {
            return ResponseEntity.badRequest()
                .body(Map.of("error", "roomId and totalVolume are required"));
        }

        sensorService.process(payload);

        return ResponseEntity.ok(Map.of(
            "status", "ok",
            "roomId", payload.getRoomId(),
            "volumePct", String.format("%.1f", payload.getVolumePct()),
            "received", Instant.now().toString()
        ));
    }

    @PostMapping("/clamp")
    public ResponseEntity<Map<String, Object>> emergencyClamp(
            @RequestBody ClampRequest req) {

        String action = req.getAction();
        String roomId = req.getRoomId();

        if (!"LOCK".equalsIgnoreCase(action) && !"UNLOCK".equalsIgnoreCase(action)) {
            return ResponseEntity.badRequest()
                .body(Map.of("error", "action must be LOCK or UNLOCK"));
        }

        log.warn("EMERGENCY CLAMP — Room {} | action={} | patient={}",
            roomId, action, req.getPatientId());

        mqttService.sendValveCommand(roomId, action.toUpperCase());

        broadcaster.broadcastClampAck(roomId, action.toUpperCase());

        if ("LOCK".equalsIgnoreCase(action)) {
            broadcaster.broadcastAlert(roomId, "INFO",
                "EMERGENCY CLAMP ACTIVATED for Room " + roomId + " — Operator action");
        } else {
            broadcaster.broadcastAlert(roomId, "INFO",
                "Clamp RELEASED for Room " + roomId + " — Monitoring resumed");
        }

        return ResponseEntity.ok(Map.of(
            "status", "ok",
            "roomId", roomId,
            "action", action.toUpperCase(),
            "sent", Instant.now().toString()
        ));
    }

    @GetMapping("/health")
    public ResponseEntity<Map<String, String>> health() {
        return ResponseEntity.ok(Map.of(
            "status", "UP",
            "service", "IVGuard Backend",
            "time", Instant.now().toString()
        ));
    }
}
