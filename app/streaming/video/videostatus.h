#pragma once
#include <Limelight.h>
#include <QJsonObject>
#include <QJsonArray>

namespace VideoStatus {
inline QJsonObject read() {
    uint32_t rtt = 0, variance = 0;
    LiGetEstimatedRttInfo(&rtt, &variance);
    QJsonObject result{{"hdr",LiGetCurrentHostDisplayHdrMode()}, {"rtt",double(rtt)}, {"variance",double(variance)}};
    SS_HDR_METADATA hdr{};
    if (LiGetHdrMetadata(&hdr)) {
        QJsonArray values;
        for (const auto& primary : hdr.displayPrimaries) { values.append(primary.x); values.append(primary.y); }
        values.append(hdr.whitePoint.x); values.append(hdr.whitePoint.y);
        values.append(hdr.maxDisplayLuminance); values.append(hdr.minDisplayLuminance);
        values.append(hdr.maxContentLightLevel); values.append(hdr.maxFrameAverageLightLevel);
        values.append(hdr.maxFullFrameLuminance);
        result["mastering"] = values;
    }
    return result;
}
inline bool mastering(const QJsonObject& status, SS_HDR_METADATA& hdr) {
    const auto values = status.value("mastering").toArray();
    if (values.size() != 13) return false;
    for (const auto& value : values) {
        const auto number = value.toDouble(-1);
        if (number < 0 || number > 65535 || int(number) != number) return false;
    }
    int i = 0;
    for (auto& primary : hdr.displayPrimaries) { primary.x = values[i++].toInt(); primary.y = values[i++].toInt(); }
    hdr.whitePoint.x = values[i++].toInt(); hdr.whitePoint.y = values[i++].toInt();
    hdr.maxDisplayLuminance = values[i++].toInt(); hdr.minDisplayLuminance = values[i++].toInt();
    hdr.maxContentLightLevel = values[i++].toInt(); hdr.maxFrameAverageLightLevel = values[i++].toInt();
    hdr.maxFullFrameLuminance = values[i].toInt();
    return true;
}
}
