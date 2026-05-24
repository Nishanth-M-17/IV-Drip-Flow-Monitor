package com.ivguard.service;

import com.ivguard.model.SensorPayload;
import com.ivguard.websocket.WebSocketBroadcaster;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

@Service
public class SensorProcessingService {

    private static final Logger log = LoggerFactory.getLogger(SensorProcessingService.class);

    @Value("${ivguard.threshold.critical:0.12}")
    private double criticalThreshold;

    @Value("${ivguard.threshold.warning:0.15}")
    private double warningThreshold;

    private final WebSocketBroadcaster broadcaster;

    private final Map<String, Boolean> criticalFired  = new ConcurrentHashMap<>();
    private final Map<String, Boolean> warningFired   = new ConcurrentHashMap<>();

    public SensorProcessingService(WebSocketBroadcaster broadcaster) {
        this.broadcaster = broadcaster;
    }

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

        broadcaster.broadcastSensorUpdate(roomId, dpm, volumePct);

        if (current <= total * criticalThreshold) {
            if (!criticalFired.getOrDefault(roomId, false)) {
                criticalFired.put(roomId, true);
                warningFired.put(roomId, true);
                String msg = String.format(
                    "CRITICAL: Room %s volume at %.1f%% (%.0fml) — below %d%% threshold",
                    roomId, volumePct, current, Math.round(criticalThreshold * 100));
                broadcaster.broadcastAlert(roomId, "CRITICAL", msg);
                log.warn(msg);
            }

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
            criticalFired.put(roomId, false);
            warningFired.put(roomId, false);
        }
    }
}
