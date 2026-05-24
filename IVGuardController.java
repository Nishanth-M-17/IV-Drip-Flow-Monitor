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

/**
 * REST Controller — two groups of endpoints:
 *
 * 1. ESP32-S3 → Server
 *    POST /api/sensor-data   (ESP32 pushes drip readings)
 *
 * 2. Frontend → Server → Hardware
 *    POST /api/clamp         (dashboard Emergency Clamp button)
 *    GET  /api/health        (simple liveness probe)
 */
@RestController
@RequestMapping("/api")
@CrossOrigin(origins = "*")   // tighten in production
public class IVGuardController {

    private static final Logger log = LoggerFactory.getLogger(IVGuardController.class);

    private final SensorProcessingService sensorService;
    private final MqttService             mqttService;
    private final WebSocketBroadcaster    broadcaster;

    public IVGuardController(SensorProcessingService sensorService,
                             MqttService mqttService,
                             WebSocketBroadcaster broadcaster) {
        this.sensorService = sensorService;
        this.mqttService   = mqttService;
        this.broadcaster   = broadcaster;
    }

    /* ────────────────────────────────────────────────────────
       POST /api/sensor-data
       Called by the ESP32-S3 after each drip event.

       Example request body:
       {
         "roomId":        "201",
         "patientId":     "john-doe",
         "currentVolume": 360.0,
         "totalVolume":   500.0,
         "dpm":           64.2,
         "timestamp":     1715000000000
       }
    ──────────────────────────────────────────────────────── */
    @PostMapping("/sensor-data")
    public ResponseEntity<Map<String, Object>> receiveSensorData(
            @RequestBody SensorPayload payload) {

        log.debug("POST /api/sensor-data ← Room {} | vol={}/{} dpm={}",
            payload.getRoomId(), payload.getCurrentVolume(),
            payload.getTotalVolume(), payload.getDpm());

        /* Validate */
        if (payload.getRoomId() == null || payload.getTotalVolume() <= 0) {
            return ResponseEntity.badRequest()
                .body(Map.of("error", "roomId and totalVolume are required"));
        }

        /* Core logic: threshold check + WebSocket broadcast */
        sensorService.process(payload);

        return ResponseEntity.ok(Map.of(
            "status",    "ok",
            "roomId",    payload.getRoomId(),
            "volumePct", String.format("%.1f", payload.getVolumePct()),
            "received",  Instant.now().toString()
        ));
    }

    /* ────────────────────────────────────────────────────────
       POST /api/clamp
       Called by the frontend Emergency Clamp button.

       Example request body:
       {
         "roomId":    "201",
         "patientId": "john-doe",
         "action":    "LOCK",
         "timestamp": "2024-05-07T15:30:00.000Z"
       }
    ──────────────────────────────────────────────────────── */
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

        /* 1. Publish MQTT command to the hardware servo */
        mqttService.sendValveCommand(roomId, action.toUpperCase());

        /* 2. Broadcast acknowledgement back to all frontends */
        broadcaster.broadcastClampAck(roomId, action.toUpperCase());

        /* 3. Optionally broadcast an alert so nurses see it in the alerts log */
        if ("LOCK".equalsIgnoreCase(action)) {
            broadcaster.broadcastAlert(roomId, "INFO",
                "EMERGENCY CLAMP ACTIVATED for Room " + roomId + " — Operator action");
        } else {
            broadcaster.broadcastAlert(roomId, "INFO",
                "Clamp RELEASED for Room " + roomId + " — Monitoring resumed");
        }

        return ResponseEntity.ok(Map.of(
            "status",  "ok",
            "roomId",  roomId,
            "action",  action.toUpperCase(),
            "sent",    Instant.now().toString()
        ));
    }

    /* ────────────────────────────────────────────────────────
       GET /api/health  — used by monitoring / load balancers
    ──────────────────────────────────────────────────────── */
    @GetMapping("/health")
    public ResponseEntity<Map<String, String>> health() {
        return ResponseEntity.ok(Map.of(
            "status",  "UP",
            "service", "IVGuard Backend",
            "time",    Instant.now().toString()
        ));
    }
}
