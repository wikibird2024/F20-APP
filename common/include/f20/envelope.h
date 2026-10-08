#pragma once
#include "f20/jsonread.h"
#include "f20/results.h"

#include <chrono>
#include <optional>
#include <string>

namespace f20 {

// The company MQTT message format (spec 6.6): two topics per machine and
// one JSON envelope. Every key on the wire is snake_case; the mapping from
// our camelCase types lives here and nowhere else.
//
//   {"command": "status", "command_type": "request", "data": {},
//    "machine_name": "F20", "machine_sn": "09A006",
//    "transaction_id": "4941-20240907-141649"}
struct Envelope {
    std::string command;     // status, result, alarm, measure, ...
    std::string commandType; // "request", "response" or "ack"
    json data = json::object();
    std::string machineName;   // "F20" from F20APP, "AR" from the server
    std::string machineSn;     // "09A006"
    std::string transactionId; // new per request; copied into its response or ack
};

std::string serialize(const Envelope& envelope);
// nullopt unless the text is a JSON object with string command,
// command_type and transaction_id. data defaults to {}; a data that is not
// an object, or a wrong-typed optional field, is rejected too. Never throws.
std::optional<Envelope> parseEnvelope(const std::string& text);

// "NNNN-yyyyMMdd-HHmmss": a counter (1-9999, wraps) and the UTC time.
std::string makeTransactionId(int counter, std::chrono::system_clock::time_point utc);
// UTC time as the server expects it: 2026-10-07T08:15:30.120Z
std::string isoUtc(std::chrono::system_clock::time_point utc);

// "F20:09A006" -> "09A006": machine_sn and the topics use the bare number.
std::string bareSerial(const std::string& serial);
std::string sendTopic(const std::string& serial);    // "09A006/ar/f20/send"
std::string receiveTopic(const std::string& serial); // "09A006/ar/f20/receive"

// What a stored result carries beside its layers (spec 6.6.5.2).
struct ResultFacts {
    std::string resultId;
    std::string measuredAtUtc; // isoUtc()
    std::string recipeName;
    std::string sampleId;
    std::string operatorName;
    std::optional<int> baselineAgeMinutes;
    std::string reanalyzedFrom; // result id of the original; "" for a measurement
};

// The data of a result message and of a successful measure response:
// result_id, measured_at, ..., layers[{layer, thickness_nm, n?, k?,
// roughness_nm?}], error "". n, k and roughness_nm only when solved.
json serverResultData(const MeasureResult& result, const ResultFacts& facts);

} // namespace f20
