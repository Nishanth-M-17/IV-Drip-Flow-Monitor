package com.ivguard.service;

import com.ivguard.model.SensorPayload;
import com.ivguard.websocket.WebSocketBroadcaster;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Core business logic service.
 *
 * Processes every incoming sensor reading (from REST or MQTT),
 * checks volume thresholds, and pushes live data + alerts to
 * the frontend via WebSocket.
 */
@Service
public class SensorProcessingService {

    private static final Logger log = LoggerFactory.getLogger(SensorProcessingService.class);

    @Value("${ivguard.threshold.critical:0.12}")
    private double criticalThreshold;   // 12%

    @Value("${ivguard.threshold.warning:0.15}")
    private double warningThreshold;    // 15%

    private final WebSocketBroadcaster broadcaster;

    /**
     * Track whether we've already fired a critical alert for each room
     * in this session so we don't spam alerts.
     */
    private final Map<String, Boolean> criticalFired  = new ConcurrentHashMap<>();
    private final Map<String, Boolean> warningFired   = new ConcurrentHashMap<>();

    public SensorProcessingService(WebSocketBroadcaster broadcaster) {
        this.broadcaster = broadcaster;
    }

    /**
     * Main entry point — called by both the REST controller and the MQTT listener.
     */
    public void process(SensorPayload payload) {
        double total   = payload.getTotalVolume();
        double current = payload.getCurrentVolume();
        double dpm     = payload.getDpm();
        String roomId  = payload.getRoomId();

        if (total <= 0) {
            log.warn("Room {} sent totalVolume=0 — ignoring", roomId);
            return;
        }

        double volumePct = (current / total) * 100.0;
        log.debug("Room {} | DPM={} | Volume={}ml / {}ml ({}%)", roomId, dpm, current, total, String.format("%.1f", volumePct));

        /* ── Broadcast live update to dashboard ── */
        broadcaster.broadcastSensorUpdate(roomId, dpm, volumePct);

        /* ── Critical threshold check ── */
        if (current <= total * criticalThreshold) {
            if (!criticalFired.getOrDefault(roomId, false)) {
                criticalFired.put(roomId, true);
                warningFired.put(roomId, true);   // also suppress warning if critical fires
                String msg = String.format(
                    "CRITICAL: Room %s volume at %.1f%% (%.0fml) — below %d%% threshold",
                    roomId, volumePct, current, Math.round(criticalThreshold * 100));
                broadcaster.broadcastAlert(roomId, "CRITICAL", msg);
                log.warn(msg);
            }

        /* ── Warning threshold check ── */
        } else if (current <= total * warningThreshold) {
            if (!warningFired.getOrDefault(roomId, false)) {
                warningFired.put(roomId, true);
                String msg = String.format(
                    "WARNING: Room %s Low Volume Threshold Approaching (%.1f%%)",
                    roomId, volumePct);
                broadcaster.broadcastAlert(roomId, "WARNING", msg);
                log.warn(msg);
            }

        } else {
            /* Volume recovered above thresholds — reset flags */
            criticalFired.put(roomId, false);
            warningFired.put(roomId, false);
        }
    }
}
