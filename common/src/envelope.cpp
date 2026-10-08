#include "f20/envelope.h"

#include <cstdio>
#include <ctime>

namespace f20 {
namespace {

std::tm utcFields(std::chrono::system_clock::time_point utc) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(utc);
    std::tm fields{};
#ifdef _WIN32
    gmtime_s(&fields, &seconds);
#else
    gmtime_r(&seconds, &fields);
#endif
    return fields;
}

// An optional string field: missing is fine (""), another type is not.
bool readOptionalString(const json& object, const char* key, std::string& out) {
    const auto it = object.find(key);
    if (it == object.end())
        return true;
    if (!it->is_string())
        return false;
    out = it->get<std::string>();
    return true;
}

} // namespace

std::string serialize(const Envelope& envelope) {
    const json message{{"command", envelope.command},
                       {"command_type", envelope.commandType},
                       {"data", envelope.data.is_object() ? envelope.data : json::object()},
                       {"machine_name", envelope.machineName},
                       {"machine_sn", envelope.machineSn},
                       {"transaction_id", envelope.transactionId}};
    // replace: a bad UTF-8 byte in operator text must not throw here.
    return message.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::optional<Envelope> parseEnvelope(const std::string& text) {
    const json message = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!message.is_object())
        return std::nullopt;
    Envelope envelope;
    const auto command = stringAt(message, "command");
    const auto commandType = stringAt(message, "command_type");
    const auto transactionId = stringAt(message, "transaction_id");
    if (!command || !commandType || !transactionId)
        return std::nullopt;
    envelope.command = *command;
    envelope.commandType = *commandType;
    envelope.transactionId = *transactionId;
    if (!readOptionalString(message, "machine_name", envelope.machineName) ||
        !readOptionalString(message, "machine_sn", envelope.machineSn))
        return std::nullopt;
    if (const auto it = message.find("data"); it != message.end()) {
        if (!it->is_object())
            return std::nullopt;
        envelope.data = *it;
    }
    return envelope;
}

std::string makeTransactionId(int counter, std::chrono::system_clock::time_point utc) {
    const int number = ((counter - 1) % 9999 + 9999) % 9999 + 1; // 1..9999
    const std::tm t = utcFields(utc);
    char text[64]; // room for any int the compiler can imagine
    std::snprintf(text, sizeof text, "%04d-%04d%02d%02d-%02d%02d%02d", number, t.tm_year + 1900,
                  t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return text;
}

std::string isoUtc(std::chrono::system_clock::time_point utc) {
    const std::tm t = utcFields(utc);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        utc.time_since_epoch()).count() % 1000;
    char text[80];
    std::snprintf(text, sizeof text, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", t.tm_year + 1900,
                  t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
                  static_cast<int>(ms < 0 ? ms + 1000 : ms));
    return text;
}

std::string bareSerial(const std::string& serial) {
    return serial.rfind("F20:", 0) == 0 ? serial.substr(4) : serial;
}

std::string sendTopic(const std::string& serial) {
    return bareSerial(serial) + "/ar/f20/send";
}

std::string receiveTopic(const std::string& serial) {
    return bareSerial(serial) + "/ar/f20/receive";
}

json serverResultData(const MeasureResult& result, const ResultFacts& facts) {
    json layers = json::array();
    for (const LayerResult& layer : result.layers) {
        json entry{{"layer", layer.layer}, {"thickness_nm", layer.thicknessNm}};
        if (layer.n)
            entry["n"] = *layer.n;
        if (layer.k)
            entry["k"] = *layer.k;
        if (layer.roughnessNm)
            entry["roughness_nm"] = *layer.roughnessNm;
        layers.push_back(std::move(entry));
    }
    json data{{"result_id", facts.resultId},
              {"measured_at", facts.measuredAtUtc},
              {"recipe_name", facts.recipeName},
              {"sample_id", facts.sampleId},
              {"operator_name", facts.operatorName},
              {"passed", result.passed},
              {"gof", result.gof},
              {"layers", std::move(layers)},
              {"reanalyzed_from", facts.reanalyzedFrom},
              {"error", ""}};
    data["baseline_age_minutes"] =
        facts.baselineAgeMinutes ? json(*facts.baselineAgeMinutes) : json(nullptr);
    return data;
}

} // namespace f20
